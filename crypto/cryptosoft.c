/****************************************************************************
 * crypto/cryptosoft.c
 *
 * SPDX-License-Identifier: OAR
 * SPDX-FileCopyrightText: Copyright (c) 2000, 2001 Angelos D. Keromytis
 * SPDX-FileContributor: Angelos D. Keromytis (angelos@cis.upenn.edu)
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all source code copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 *
 * This code was written by Angelos D. Keromytis in Athens, Greece, in
 * February 2000. Network Security Technologies Inc. (NSTI) kindly
 * supported the development of this code.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <endian.h>
#include <fcntl.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mtd/configdata.h>
#include <nuttx/kmalloc.h>
#include <nuttx/lib/math32.h>
#include <crypto/cryptodev.h>
#include <crypto/cryptosoft.h>
#include <crypto/curve25519.h>
#include <crypto/ecc.h>
#include <crypto/xform.h>
#include <crypto/rsa.h>
#include <crypto/sha2.h>
#include <sys/param.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT

#define SWKEY_MAGIC_STRING  "SWKEYMGMT"
#define SWKEY_FILL_NAME(name, keyid, type)       \
  do                                             \
    {                                            \
      snprintf(name, sizeof(name), "%s.%lu.%s",  \
               SWKEY_MAGIC_STRING, keyid, type); \
    }                                            \
  while (0)

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

struct swkey_data_s
{
  uint32_t id;
  uint32_t size;
  uint32_t flags;
  uint8_t buf[CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_BUFSIZE];
  TAILQ_ENTRY(swkey_data_s) next;
};

struct swkey_context_s
{
  struct file file;
  TAILQ_HEAD(swkey_list, swkey_data_s) head;
};

#endif /* CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT */

#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO

/* krp_param[] slots shared by every CRK_* RSA operation.  Slot 0 is the
 * operand, so comp[0] doubles as the working value.
 */

enum swcr_rsa_slot_e
{
  SWCR_RSA_X = 0,
  SWCR_RSA_N,
  SWCR_RSA_E,
  SWCR_RSA_D,
  SWCR_RSA_P,
  SWCR_RSA_Q,
  SWCR_RSA_DP,
  SWCR_RSA_DQ,
  SWCR_RSA_QP,
  SWCR_RSA_NSLOTS
};

struct swcr_rsa_s
{
  uint8_t comp[SWCR_RSA_NSLOTS][RSA_MAX_BYTES];  /* big-endian */
  size_t len[SWCR_RSA_NSLOTS];
};

#endif /* CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO */

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO

FAR struct swcr_data **swcr_sessions = NULL;
uint32_t swcr_sesnum = 0;
int swcr_id = -1;

#endif /* CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT

/* key data operations in flash */

/****************************************************************************
 * Name: swkey_write
 *
 * Description:
 *   Storing key data into flash and mapping it to the keyid
 *
 ****************************************************************************/

static int swkey_write(FAR struct file *filep, uint32_t keyid,
                       FAR const void *data, uint32_t len, int flags)
{
  struct config_data_s config;
  int ret;

  if (keyid == 0 || data == NULL || len == 0)
    {
      return -EINVAL;
    }

  /* Write the data and flags to the Flash */

  memset(&config, 0, sizeof(config));
  SWKEY_FILL_NAME(config.name, keyid, "data");
  config.len = len;
  config.configdata = (uint8_t *)data;
  ret = file_ioctl(filep, CFGDIOC_SETCONFIG, &config);
  if (ret < 0)
    {
      return ret;
    }

  SWKEY_FILL_NAME(config.name, keyid, "flags");
  config.len = sizeof(uint32_t);
  config.configdata = (uint8_t *)&flags;
  return file_ioctl(filep, CFGDIOC_SETCONFIG, &config);
}

/****************************************************************************
 * Name: swkey_remove
 *
 * Description:
 *   Removing key data from flash
 *
 ****************************************************************************/

static int swkey_remove(FAR struct file *filep, uint32_t keyid)
{
  struct config_data_s config;
  int ret;

  if (keyid == 0)
    {
      return -EINVAL;
    }

  /* Remove the flags and data */

  memset(&config, 0, sizeof(config));
  SWKEY_FILL_NAME(config.name, keyid, "flags");
  ret = file_ioctl(filep, CFGDIOC_DELCONFIG, &config);
  if (ret < 0)
    {
      return ret;
    }

  memset(config.name, 0, sizeof(config.name));
  SWKEY_FILL_NAME(config.name, keyid, "data");
  return file_ioctl(filep, CFGDIOC_DELCONFIG, &config);
}

/****************************************************************************
 * Name: swkey_get_flags
 *
 * Description:
 *   Getting key flags from flash
 *
 ****************************************************************************/

static int swkey_get_flags(FAR struct file *filep, uint32_t keyid,
                           FAR uint32_t *flags)
{
  struct config_data_s config;

  if (keyid == 0 || flags == NULL)
    {
      return -EINVAL;
    }

  memset(&config, 0, sizeof(config));
  SWKEY_FILL_NAME(config.name, keyid, "flags");
  config.len = sizeof(uint32_t);
  config.configdata = (uint8_t *)flags;
  return file_ioctl(filep, CFGDIOC_GETCONFIG, &config);
}

/****************************************************************************
 * Name: swkey_read
 *
 * Description:
 *   Getting key data from flash
 *
 ****************************************************************************/

static int swkey_read(FAR struct file *filep, uint32_t keyid,
                      FAR void *buf, uint32_t buflen)
{
  struct config_data_s config;
  int ret;

  if (keyid == 0 || buf == NULL)
    {
      return -EINVAL;
    }

  memset(&config, 0, sizeof(config));
  SWKEY_FILL_NAME(config.name, keyid, "data");
  config.len = buflen;
  config.configdata = buf;
  ret = file_ioctl(filep, CFGDIOC_GETCONFIG, &config);
  if (ret < 0)
    {
      return ret;
    }

  return config.len;
}

/* key data operations in cache */

/****************************************************************************
 * Name: swkey_get_context
 *
 * Description:
 *   Access key cache list entries
 *
 ****************************************************************************/

static FAR struct swkey_context_s *swkey_get_context(void)
{
  FAR struct swkey_context_s *ctx;
  int swkey_id;

  swkey_id = crypto_find_driverid(CRYPTOCAP_F_KEY_MGMT);
  if (swkey_id < 0)
    {
      return NULL;
    }

  ctx = (FAR struct swkey_context_s *)crypto_driver_get_priv(swkey_id);
  if (ctx == NULL)
    {
      return NULL;
    }

  if (ctx->file.f_inode == NULL)
    {
      if (file_open(&ctx->file,
                    CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_DEVICE,
                    O_RDWR | O_CLOEXEC) < 0)
        {
          return NULL;
        }
    }

  return ctx;
}

/****************************************************************************
 * Name: swkey_get_cache_data
 *
 * Description:
 *   Acquire an available key slot in cache. If the key exists in the cache,
 * utilize that slot immediately; otherwise, locate the last slot.
 *
 ****************************************************************************/

static FAR struct swkey_data_s *
swkey_get_cache_data(FAR struct swkey_context_s *ctx, uint32_t keyid)
{
  FAR struct swkey_data_s *data;

  TAILQ_FOREACH(data, &ctx->head, next)
    {
      if (data->id == keyid)
        {
          break;
        }
    }

  if (data == NULL)
    {
      data = TAILQ_LAST(&ctx->head, swkey_list);
      if (data->id)
        {
          swkey_write(&ctx->file, data->id, data->buf,
                      data->size, data->flags);
        }
    }

  return data;
}

/****************************************************************************
 * Name: swkey_promote_cache_data
 *
 * Description:
 *   Update the key cache linked list.
 *   Move the accessed key cache to the head position to ensure
 *   the most frequently used keys remain cached.
 *
 ****************************************************************************/

static void swkey_promote_cache_data(FAR struct swkey_context_s *ctx,
                                     FAR struct swkey_data_s *data)
{
  TAILQ_REMOVE(&ctx->head, data, next);
  TAILQ_INSERT_HEAD(&ctx->head, data, next);
}

/* key management operations */

/****************************************************************************
 * Name: swkey_clean_cache_data
 *
 * Description:
 *   Clean the cache slot
 *
 ****************************************************************************/

static void swkey_clean_cache_data(FAR struct swkey_data_s *data)
{
  explicit_bzero(data->buf, sizeof(data->buf));
  data->id = 0;
  data->size = 0;
  data->flags = 0;
}

/****************************************************************************
 * Name: swkey_is_valid
 *
 * Description:
 *   Check whether the given keyid is available in the driver
 *
 ****************************************************************************/

static int swkey_is_valid(FAR struct swkey_context_s *ctx, uint32_t keyid)
{
  uint32_t flags;
  int ret;

  if (keyid == 0)
    {
      return -EINVAL;
    }

  ret = swkey_get_flags(&ctx->file, keyid, &flags);
  if (ret == -ENOENT)
    {
      /* No such file means keyid unused and available
       * and occupied this keyid with MAGIC-STRING
       */

      return swkey_write(&ctx->file, keyid, SWKEY_MAGIC_STRING,
                                     sizeof(SWKEY_MAGIC_STRING), 0);
    }
  else if (ret == 0)
    {
      /* success means keyid used and unavailable */

      return -EEXIST;
    }

  return ret;
}

/****************************************************************************
 * Name: swkey_alloc
 *
 * Description:
 *   Acquire an available key ID from the driver
 *
 ****************************************************************************/

static int swkey_alloc(FAR struct swkey_context_s *ctx,
                       FAR uint32_t *keyid)
{
  int i;

  for (i = 1; i <= CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_NKEYS; i++)
    {
      if (swkey_is_valid(ctx, i) == 0)
        {
          *keyid = i;
          return OK;
        }
    }

  return -ENOMEM;
}

/****************************************************************************
 * Name: swkey_import
 *
 * Description:
 *   Import key data into cache key slot and bind to the keyid
 *
 ****************************************************************************/

static int swkey_import(FAR struct swkey_context_s *ctx,
                        uint32_t keyid, FAR void *buf,
                        uint32_t buflen, uint32_t flags)
{
  FAR struct swkey_data_s *data;

  if (keyid == 0)
    {
      return -EINVAL;
    }

  if (buflen > CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_BUFSIZE)
    {
      return swkey_write(&ctx->file, keyid, buf, buflen, flags);
    }

  data = swkey_get_cache_data(ctx, keyid);
  data->id = keyid;
  data->size = buflen;
  memcpy(data->buf, buf, data->size);
  if (flags & CRYPTO_F_NOT_EXPORTABLE)
    {
      data->flags |= CRYPTO_F_NOT_EXPORTABLE;
    }
  else
    {
      data->flags &= ~CRYPTO_F_NOT_EXPORTABLE;
    }

  swkey_promote_cache_data(ctx, data);
  return OK;
}

/****************************************************************************
 * Name: swkey_delete
 *
 * Description:
 *   Remove a specific key by keyid
 *
 ****************************************************************************/

static int swkey_delete(FAR struct swkey_context_s *ctx, uint32_t keyid)
{
  FAR struct swkey_data_s *data;

  if (keyid == 0)
    {
      return -EINVAL;
    }

  data = swkey_get_cache_data(ctx, keyid);
  if (data->id == keyid)
    {
      swkey_clean_cache_data(data);
    }

  return swkey_remove(&ctx->file, keyid);
}

/****************************************************************************
 * Name: swkey_export
 *
 * Description:
 *   Export key data by keyid
 *
 ****************************************************************************/

static int swkey_export(FAR struct swkey_context_s *ctx,
                        uint32_t keyid, FAR void *buf,
                        uint32_t buflen)
{
  FAR struct swkey_data_s *data;
  uint32_t flags;
  int ret;

  if (keyid == 0)
    {
      return -EINVAL;
    }

  data = swkey_get_cache_data(ctx, keyid);
  if (data->id == keyid)
    {
      /* Key in cache, export data and update cache */

      if (data->flags & CRYPTO_F_NOT_EXPORTABLE)
        {
          return -EACCES;
        }

      if (buflen < data->size)
        {
          return -ENOBUFS;
        }

      memcpy(buf, data->buf, data->size);
      swkey_promote_cache_data(ctx, data);
      return data->size;
    }

  /* Key not in cache, get key from flash */

  ret = swkey_get_flags(&ctx->file, keyid, &flags);
  if (ret < 0)
    {
      return ret;
    }

  if (flags & CRYPTO_F_NOT_EXPORTABLE)
    {
      return -EACCES;
    }

  ret = swkey_read(&ctx->file, keyid, buf, buflen);
  if (ret < 0)
    {
      return ret;
    }
  else if (ret > buflen)
    {
      return -ENOBUFS;
    }
  else if (memcmp(buf, SWKEY_MAGIC_STRING, ret) == 0)
    {
      return -ENOENT;
    }

  if (ret < CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_BUFSIZE)
    {
      data->id = keyid;
      data->size = ret;
      data->flags = flags;
      memcpy(data->buf, buf, ret);
      swkey_promote_cache_data(ctx, data);
    }

  return ret;
}

/****************************************************************************
 * Name: swkey_gen_secp256r1_key
 *
 * Description:
 *   Generate SECP256R1 keypair and bound with keyid
 *
 ****************************************************************************/

static int swkey_gen_secp256r1_key(FAR struct swkey_context_s *ctx,
                                   uint32_t priv_keyid,
                                   uint32_t pub_keyid)
{
  FAR struct swkey_data_s *data;
  uint8_t priv[secp256r1];
  uint8_t pub[secp256r1 * 2];
  int ret = -EINVAL;

  if (priv_keyid == 0 || pub_keyid == 0)
    {
      return ret;
    }

  if (ecc_make_key_uncomp(pub, pub + secp256r1, priv) == 0)
    {
      return ret;
    }

  /* Private keys cannot be exported */

  ret = swkey_write(&ctx->file, priv_keyid, priv, secp256r1,
                    CRYPTO_F_NOT_EXPORTABLE);
  if (ret < 0)
    {
      return ret;
    }

  ret = swkey_write(&ctx->file, pub_keyid, pub, secp256r1 * 2, 0);
  if (ret < 0)
    {
      swkey_delete(ctx, priv_keyid);
      return ret;
    }

  if (CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_BUFSIZE >= secp256r1)
    {
      data = swkey_get_cache_data(ctx, priv_keyid);
      data->id = priv_keyid;
      data->size = secp256r1;
      data->flags = CRYPTO_F_NOT_EXPORTABLE;
      memcpy(data->buf, priv, secp256r1);
      swkey_promote_cache_data(ctx, data);
    }

  if (CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_BUFSIZE >= secp256r1 * 2)
    {
      data = swkey_get_cache_data(ctx, pub_keyid);
      data->id = pub_keyid;
      data->size = secp256r1 * 2;
      data->flags = 0;
      memcpy(data->buf, pub, secp256r1 * 2);
      swkey_promote_cache_data(ctx, data);
    }

  return ret;
}

/****************************************************************************
 * Name: swkey_gen_aes_key
 *
 * Description:
 *   Generate AES key and bound with keyid
 *
 ****************************************************************************/

static int swkey_gen_aes_key(FAR struct swkey_context_s *ctx, uint32_t keyid,
                             uint32_t keylen)
{
  FAR struct swkey_data_s *data;
  int ret = -EINVAL;
  char buf[32];

  if (keyid == 0)
    {
      return ret;
    }

  /* Generate a key sufficient for AES-128/192/256 */

  arc4random_buf(buf, keylen);
  ret = swkey_write(&ctx->file, keyid, buf, keylen, 0);
  if (ret < 0)
    {
      return ret;
    }

  if (keylen <= CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_BUFSIZE)
    {
      data = swkey_get_cache_data(ctx, keyid);
      data->id = keyid;
      data->size = keylen;
      data->flags = 0;
      memcpy(data->buf, buf, keylen);
      swkey_promote_cache_data(ctx, data);
    }

  return ret;
}

/****************************************************************************
 * Name: swkey_save
 *
 * Description:
 *   Write key from cache to Flash
 *
 ****************************************************************************/

static int swkey_save(FAR struct swkey_context_s *ctx, uint32_t keyid)
{
  FAR struct swkey_data_s *data;
  int ret = -EINVAL;

  if (keyid == 0)
    {
      return ret;
    }

  data = swkey_get_cache_data(ctx, keyid);
  if (data->id == keyid)
    {
      ret = swkey_write(&ctx->file, keyid, data->buf,
                        data->size, data->flags);
      if (ret < 0)
        {
          return ret;
        }

      swkey_promote_cache_data(ctx, data);
    }

  return OK;
}

/****************************************************************************
 * Name: crypto_load_key
 *
 * Description:
 *   Load key data from Flash to cache
 *
 ****************************************************************************/

static int swkey_load(FAR struct swkey_context_s *ctx, uint32_t keyid)
{
  FAR struct swkey_data_s *data;
  char buf[CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_BUFSIZE];
  int readlen;

  if (keyid == 0)
    {
      return -EINVAL;
    }

  readlen = swkey_read(&ctx->file, keyid, buf, sizeof(buf));
  if (readlen < 0)
    {
      return readlen;
    }
  else if (readlen > CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_BUFSIZE)
    {
      return -EFBIG;
    }

  data = swkey_get_cache_data(ctx, keyid);
  data->id = keyid;
  data->size = readlen;
  swkey_get_flags(&ctx->file, keyid, &data->flags);
  memcpy(data->buf, buf, data->size);
  swkey_promote_cache_data(ctx, data);
  return OK;
}

/****************************************************************************
 * Name: swkey_unload
 *
 * Description:
 *   Unload key data from cache
 *
 ****************************************************************************/

static int swkey_unload(FAR struct swkey_context_s *ctx, uint32_t keyid)
{
  FAR struct swkey_data_s *data;
  int ret = -EINVAL;

  if (keyid == 0)
    {
      return ret;
    }

  data = swkey_get_cache_data(ctx, keyid);
  if (data->id == keyid)
    {
      ret = swkey_write(&ctx->file, data->id, data->buf,
                        data->size, data->flags);
      if (ret < 0)
        {
          return ret;
        }

      swkey_clean_cache_data(data);
    }

  return ret;
}

/****************************************************************************
 * Name: swkey_kprocess
 *
 * Description:
 *   Key management process function in crypto driver
 *
 ****************************************************************************/

static int swkey_kprocess(FAR struct cryptkop *krp)
{
  FAR struct swkey_context_s *ctx;
  uint32_t priv_keyid;
  uint32_t pub_keyid;
  uint32_t keylen;
  uint32_t keyid;
  int ret;

  /* Sanity check */

  if (krp == NULL)
    {
      return -EINVAL;
    }

  ctx = swkey_get_context();
  if (ctx == NULL)
    {
      return -EINVAL;
    }

  if (krp->krp_param[0].crp_p == NULL ||
      krp->krp_param[0].crp_nbits != sizeof(uint32_t) * 8)
    {
      return -EINVAL;
    }

  keyid = *(FAR uint32_t *)krp->krp_param[0].crp_p;

  /* Go through crypto descriptors, processing as we go */

  switch (krp->krp_op)
    {
      case CRK_ALLOCATE_KEY:
        krp->krp_status = swkey_alloc(ctx, &keyid);
        if (krp->krp_status == 0)
          {
            memcpy(krp->krp_param[0].crp_p, &keyid, sizeof(uint32_t));
          }

        break;
      case CRK_VALIDATE_KEYID:
        krp->krp_status = swkey_is_valid(ctx, keyid);
        break;
      case CRK_IMPORT_KEY:
        krp->krp_status = swkey_import(ctx, keyid,
                                       krp->krp_param[1].crp_p,
                                       krp->krp_param[1].crp_nbits / 8,
                                       krp->krp_flags);
        break;
      case CRK_DELETE_KEY:
        krp->krp_status = swkey_delete(ctx, keyid);
        break;
      case CRK_EXPORT_KEY:
        ret = swkey_export(ctx, keyid,
                           krp->krp_param[1].crp_p,
                           krp->krp_param[1].crp_nbits / 8);
        if (ret < 0)
          {
            krp->krp_status = ret;
          }
        else
          {
            krp->krp_param[1].crp_nbits = ret * 8;
          }

        break;
      case CRK_GENERATE_AES_KEY:
        if (krp->krp_param[1].crp_nbits != sizeof(uint32_t) * 8)
          {
            return -EINVAL;
          }

        keylen = *(FAR uint32_t *)krp->krp_param[1].crp_p;
        if (keylen != 16 && keylen != 24 && keylen != 32)
          {
            return -EINVAL;
          }

        krp->krp_status = swkey_gen_aes_key(ctx, keyid, keylen);
        break;
      case CRK_GENERATE_SECP256R1_KEY:
        priv_keyid = keyid;
        if (krp->krp_param[1].crp_nbits != sizeof(uint32_t) * 8)
          {
            return -EINVAL;
          }

        pub_keyid = *(FAR uint32_t *)krp->krp_param[1].crp_p;
        krp->krp_status = swkey_gen_secp256r1_key(ctx, priv_keyid,
                                                       pub_keyid);
        break;
      case CRK_SAVE_KEY:
        krp->krp_status = swkey_save(ctx, keyid);
        break;
      case CRK_LOAD_KEY:
        krp->krp_status = swkey_load(ctx, keyid);
        break;
      case CRK_UNLOAD_KEY:
        krp->krp_status = swkey_unload(ctx, keyid);
        break;
      default:

        /* Unknown/unsupported operation */

        krp->krp_status = -EINVAL;
        break;
    }

  return OK;
}

/****************************************************************************
 * Name: swkey_context_init
 *
 * Description:
 *   Init software key ctx
 *
 ****************************************************************************/

static int swkey_context_init(FAR struct swkey_context_s *ctx)
{
  FAR struct swkey_data_s *data;
  int i;

  TAILQ_INIT(&ctx->head);
  for (i = 0; i < CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT_NSLOTS; i++)
    {
      data = (FAR struct swkey_data_s *)kmm_zalloc(sizeof(*data));
      if (data == NULL)
        {
          return -ENOMEM;
        }

      TAILQ_INSERT_HEAD(&ctx->head, data, next);
    }

  return OK;
}

/****************************************************************************
 * Name: swkey_context_cleanup
 *
 * Description:
 *   Cleanup software key ctx
 *
 ****************************************************************************/

static void swkey_context_cleanup(FAR struct swkey_context_s *ctx)
{
  FAR struct swkey_data_s *data;
  FAR struct swkey_data_s *tmp;

  TAILQ_FOREACH_SAFE(data, &ctx->head, next, tmp)
    {
      TAILQ_REMOVE(&ctx->head, data, next);
      memset(data, 0, sizeof(struct swkey_data_s));
      kmm_free(data);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* key management operations */

/****************************************************************************
 * Name: swkey_init
 *
 * Description:
 *   Register software key management driver
 *
 ****************************************************************************/

void swkey_init(void)
{
  int swkey_id = crypto_get_driverid(CRYPTOCAP_F_KEY_MGMT);
  FAR struct swkey_context_s *ctx;
  int kalgs[CRK_ALGORITHM_MAX + 1];

  ctx = (FAR struct swkey_context_s *)kmm_zalloc(sizeof(*ctx));
  if (ctx == NULL)
    {
      return;
    }

  if (swkey_context_init(ctx))
    {
      swkey_context_cleanup(ctx);
      kmm_free(ctx);
      return;
    }

  crypto_driver_set_priv(swkey_id, ctx);

  memset(kalgs, 0, sizeof(kalgs));
  kalgs[CRK_ALLOCATE_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_VALIDATE_KEYID] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_IMPORT_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_DELETE_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_EXPORT_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_GENERATE_AES_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_GENERATE_SECP256R1_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_SAVE_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_LOAD_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_UNLOAD_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;

  crypto_kregister(swkey_id, kalgs, swkey_kprocess);
}

#endif /* CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT */

#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO

/* Apply a symmetric encryption/decryption algorithm. */

int swcr_encdec(FAR struct cryptop *crp, FAR struct cryptodesc *crd,
                FAR struct swcr_data *sw, caddr_t buf)
{
  FAR char *output;
  unsigned char blk[EALG_MAX_BLOCK_LEN];
  FAR unsigned char *iv;
  FAR unsigned char *ivp;
  FAR unsigned char *nivp;
  unsigned char iv2[EALG_MAX_BLOCK_LEN];
  FAR const struct enc_xform *exf;
  int i;
  int j;
  int blks;
  int ivlen;
  int buflen;

  exf = sw->sw_exf;
  blks = exf->blocksize;
  ivlen = exf->ivsize;

  /* Initialize the IV */

  if (crd->crd_flags & CRD_F_ENCRYPT)
    {
      /* Do we need to write the IV */

      if (!(crd->crd_flags & CRD_F_IV_PRESENT))
        {
          arc4random_buf(crd->crd_iv, ivlen);
          bcopy(crd->crd_iv, buf + crd->crd_inject, ivlen);
        }
    }
  else
    {
      /* Decryption */

      /* IV explicitly provided ? */

      if (!(crd->crd_flags & CRD_F_IV_EXPLICIT))
        {
          /* Get IV off buf */

          bcopy(buf + crd->crd_inject, crd->crd_iv, ivlen);
        }
    }

  iv = crd->crd_iv;
  ivp = iv;

  /* xforms that provide a reinit method perform all IV
   * handling themselves.
   */

  if (exf->reinit && !(crd->crd_flags & CRD_F_UPDATE))
    {
      exf->reinit(sw->sw_kschedule, iv, ivlen);
    }

  i = crd->crd_len;

  buf = buf + crd->crd_skip;
  output = crp->crp_dst;
  while (i > 0)
    {
      buflen = MIN(i, exf->blocksize);
      bcopy(buf, blk, buflen);
      buf += buflen;
      if (exf->reinit)
        {
          if (crd->crd_flags & CRD_F_ENCRYPT)
            {
              exf->encrypt(sw->sw_kschedule,
                  blk, buflen);
            }
          else
            {
              exf->decrypt(sw->sw_kschedule,
                  blk, buflen);
            }
        }
      else if (crd->crd_flags & CRD_F_ENCRYPT)
        {
          /* XOR with previous block */

          for (j = 0; j < blks; j++)
            blk[j] ^= ivp[j];

          exf->encrypt(sw->sw_kschedule, blk, buflen);

          /* Keep encrypted block for XOR'ng
           * with next block
           */

          bcopy(blk, iv, blks);
          ivp = iv;
        }
      else
        {
          /* decrypt */

          /* Keep encrypted block for XOR'ing
           * with next block
           */

          nivp = (ivp == iv) ? iv2 : iv;
          bcopy(blk, nivp, blks);

          exf->decrypt(sw->sw_kschedule, blk, buflen);

          /* XOR with previous block */

          for (j = 0; j < blks; j++)
            {
              blk[j] ^= ivp[j];
            }

          ivp = nivp;
        }

      bcopy(blk, output, buflen);
      output += buflen;

      i -= buflen;

      /* Could be done... */

      if (i == 0)
        {
          break;
        }
    }

  if (crp->crp_iv && !exf->reinit)
    {
      /* Block-chaining modes without a reinit method track the running
       * IV in ivp and write it back so a follow-up request can continue
       * the chain. Ciphers that provide reinit (AES-CBC/XTS/CTR,
       * ChaCha20, ...) keep their IV/counter state inside the key
       * schedule, so ivp still points at the caller-supplied input IV
       * here; copying it back is a no-op and must be skipped, otherwise
       * a caller passing a read-only IV buffer would fault.
       */

      bcopy(ivp, crp->crp_iv, ivlen);
    }

  return 0; /* Done with encryption/decryption */
}

/* Compute keyed-hash authenticator. */

int swcr_authcompute(FAR struct cryptop *crp,
                     FAR struct cryptodesc *crd,
                     FAR struct swcr_data *sw,
                     caddr_t buf)
{
  unsigned char aalg[AALG_MAX_RESULT_LEN];
  FAR const struct auth_hash *axf = sw->sw_axf;
  int err;

  if (sw->sw_ictx == 0)
    {
      return -EINVAL;
    }

  if (crd->crd_flags & CRD_F_UPDATE)
    {
      err = axf->update(&sw->sw_ctx, (FAR uint8_t *)buf + crd->crd_skip,
                        crd->crd_len);
    }
  else
    {
      if (crd->crd_flags & CRD_F_ESN)
        {
          axf->update(&sw->sw_ctx, crd->crd_esn, 4);
        }

      axf->final(aalg, &sw->sw_ctx);
      bcopy(sw->sw_octx, &sw->sw_ctx, axf->ctxsize);
      err = axf->update(&sw->sw_ctx, aalg, axf->hashsize);
      if (err == 0)
        {
          axf->final((FAR uint8_t *)crp->crp_mac, &sw->sw_ctx);
        }
    }

  return err;
}

static int swcr_hash(FAR struct cryptop *crp,
                     FAR struct cryptodesc *crd,
                     FAR struct swcr_data *sw,
                     caddr_t buf)
{
  FAR const struct auth_hash *axf = sw->sw_axf;

  if (crd->crd_flags & CRD_F_UPDATE)
    {
      return axf->update(&sw->sw_ctx, (FAR uint8_t *)buf + crd->crd_skip,
                         crd->crd_len);
    }
  else
    {
      axf->final((FAR uint8_t *)crp->crp_mac, &sw->sw_ctx);
    }

  return 0;
}

/* Apply a combined encryption-authentication transformation */

int swcr_authenc(FAR struct cryptop *crp)
{
  uint32_t blkbuf[div_round_up(EALG_MAX_BLOCK_LEN, sizeof(uint32_t))];
  FAR u_char *blk = (u_char *)blkbuf;
  u_char aalg[AALG_MAX_RESULT_LEN];
  union authctx ctx;
  FAR struct cryptodesc *crd;
  FAR struct cryptodesc *crda = NULL;
  FAR struct cryptodesc *crde = NULL;
  FAR struct swcr_data *sw;
  FAR struct swcr_data *swa = NULL;
  FAR struct swcr_data *swe = NULL;
  FAR const struct auth_hash *axf = NULL;
  FAR const struct enc_xform *exf = NULL;
  caddr_t buf = (caddr_t)crp->crp_buf;
  caddr_t aad = (caddr_t)crp->crp_aad;
  FAR uint32_t *blkp;
  int blksz = 0;
  int ivlen = 0;
  int iskip = 0;
  int oskip = 0;
  int len;
  int i;

  for (crd = crp->crp_desc; crd; crd = crd->crd_next)
    {
      for (sw = swcr_sessions[crp->crp_sid & 0xffffffff];
           sw && sw->sw_alg != crd->crd_alg;
           sw = sw->sw_next);

      if (sw == NULL)
        {
          return -EINVAL;
        }

      switch (sw->sw_alg)
        {
          case CRYPTO_AES_GCM_16:
          case CRYPTO_AES_GMAC:
          case CRYPTO_AES_CMAC:
          case CRYPTO_CHACHA20_POLY1305:
          swe = sw;
          crde = crd;
          exf = swe->sw_exf;
          ivlen = exf->ivsize;
          break;
          case CRYPTO_AES_128_GMAC:
          case CRYPTO_AES_192_GMAC:
          case CRYPTO_AES_256_GMAC:
          case CRYPTO_AES_128_CMAC:
          case CRYPTO_AES_256_CMAC:
          case CRYPTO_CHACHA20_POLY1305_MAC:
          swa = sw;
          crda = crd;
          axf = swa->sw_axf;
          bcopy(&swa->sw_ctx, &ctx, axf->ctxsize);
          blksz = axf->blocksize;
          break;
          default:
            return -EINVAL;
        }
    }

  if (crde == NULL || crda == NULL)
    {
      return -EINVAL;
    }

  /* Supply MAC/cipher with IV */

  if (crp->crp_iv)
    {
      if (crp->crp_ivlen > 0)
        {
          ivlen = crp->crp_ivlen;
        }

      if (axf->reinit)
        {
          swa->sw_aadlen = 0;
          axf->reinit(&ctx, (FAR uint8_t *)crp->crp_iv, ivlen);
        }

      if (exf->reinit)
        {
          swe->sw_enclen = 0;
          exf->reinit(swe->sw_kschedule,
                      (FAR uint8_t *)crp->crp_iv, ivlen);
        }
    }

  /* Supply MAC with AAD */

  if (aad)
    {
      /* Section 5 of RFC 4106 specifies that AAD construction consists of
      * {SPI, ESN, SN} whereas the real packet contains only {SPI, SN}.
      * Unfortunately it doesn't follow a good example set in the Section
      * 3.3.2.1 of RFC 4303 where upper part of the ESN, located in the
      * external (to the packet) memory buffer, is processed by the hash
      * function in the end thus allowing to retain simple programming
      * interfaces and avoid kludges like the one below.
      */

      if (crda->crd_flags & CRD_F_ESN)
        {
          /* SPI */

          bcopy(aad + crda->crd_skip, blk, 4);
          iskip = 4; /* loop below will start with an offset of 4 */

          /* ESN */

          bcopy(crda->crd_esn, blk + 4, 4);
          oskip = iskip + 4; /* offset output buffer blk by 8 */
        }
      else
        {
          /* The MAC pads the AAD itself, so it may be split over requests */

          axf->update(&ctx, (FAR uint8_t *)aad, crp->crp_aadlen);
          iskip = crp->crp_aadlen;
        }

      for (i = iskip; i < crp->crp_aadlen; i += axf->hashsize)
        {
          len = MIN(crp->crp_aadlen - i, axf->hashsize - oskip);
          bcopy(aad + i, blk + oskip, len);
          bzero(blk + len + oskip, axf->hashsize - len - oskip);
          axf->update(&ctx, blk, axf->hashsize);
          oskip = 0; /* reset initial output offset */
        }

      swa->sw_aadlen += crp->crp_aadlen;
    }

  /* Do encryption/decryption with MAC */

  if (buf)
    {
      /* An empty update closes the AAD before the first payload block */

      if (swe->sw_enclen == 0)
        {
          axf->update(&ctx, NULL, 0);
        }

      swe->sw_enclen += crde->crd_len;
      for (i = 0; i < crde->crd_len; i += blksz)
        {
          len = MIN(crde->crd_len - i, blksz);
          if (len < blksz)
            {
              bzero(blk, blksz);
            }

          bcopy(buf + i, blk, len);
          if (crde->crd_flags & CRD_F_ENCRYPT)
            {
              exf->encrypt(swe->sw_kschedule, blk, len);
              axf->update(&ctx, blk, len);
            }
          else
            {
              axf->update(&ctx, blk, len);
              exf->decrypt(swe->sw_kschedule, blk, len);
            }

          if (crp->crp_dst)
            {
              bcopy(blk, crp->crp_dst + i, len);
            }
        }
    }

  /* Persist authentication context even for AAD-only updates. */

  bcopy(&ctx, &swa->sw_ctx, axf->ctxsize);

  /* Do any required special finalization */

  if (crp->crp_mac)
    {
      switch (crda->crd_alg)
        {
          case CRYPTO_AES_128_GMAC:
          case CRYPTO_AES_192_GMAC:
          case CRYPTO_AES_256_GMAC:

            /* Flush partial block if necessary */

            axf->update(&ctx, NULL, 0);

            /* length block */

            bzero(blk, axf->hashsize);
            blkp = (uint32_t *)blk + 1;
            *blkp = htobe32((uint32_t)(swa->sw_aadlen * 8));
            blkp = (uint32_t *)blk + 3;
            *blkp = htobe32((uint32_t)(swe->sw_enclen * 8));
            axf->update(&ctx, blk, axf->hashsize);
            break;

          case CRYPTO_CHACHA20_POLY1305_MAC:

            /* length block */

            bzero(blk, axf->hashsize);
            blkp = (uint32_t *)blk;
            *blkp = htole32(swa->sw_aadlen);
            blkp = (uint32_t *)blk + 2;
            *blkp = htole32(swe->sw_enclen);
            axf->update(&ctx, blk, axf->hashsize);
            break;
        }

      /* Finalize MAC */

      axf->final(aalg, &ctx);

      /* Inject the authentication data */

      bcopy(aalg, crp->crp_mac, axf->authsize);
    }

  return 0;
}

/* Apply a compression/decompression algorithm */

int swcr_compdec(FAR struct cryptodesc *crd, FAR struct swcr_data *sw,
                 caddr_t buf, int outtype)
{
  FAR uint8_t *data;
  FAR uint8_t *out;
  FAR const struct comp_algo *cxf;
  uint32_t result;

  cxf = sw->sw_cxf;

  /* We must handle the whole buffer of data in one time
   * then if there is not all the data in the mbuf, we must
   * copy in a buffer.
   */

  data = kmm_malloc(crd->crd_len);
  if (data == NULL)
    {
      return -EINVAL;
    }

  bcopy(buf + crd->crd_skip, data, crd->crd_len);

  if (crd->crd_flags & CRD_F_COMP)
    {
      result = cxf->compress(data, crd->crd_len, &out);
    }
  else
    {
      result = cxf->decompress(data, crd->crd_len, &out);
    }

  kmm_free(data);
  if (result == 0)
    {
      return -EINVAL;
    }

  sw->sw_size = result;

  /* Check the compressed size when doing compression */

  if (crd->crd_flags & CRD_F_COMP)
    {
      if (result > crd->crd_len)
        {
          /* Compression was useless, we lost time */

          kmm_free(out);
          return 0;
        }
    }

  bcopy(out, buf + crd->crd_skip, result);
  kmm_free(out);
  return 0;
}

/* Reserve a free slot in swcr_sessions, growing it if needed.
 * Returns the slot index, or 0 on allocation failure.
 */

static uint32_t swcr_allocsid(void)
{
  FAR struct swcr_data **swd;
  uint32_t i = 0;

  if (swcr_sessions)
    {
      for (i = 1; i < swcr_sesnum; i++)
        {
          if (swcr_sessions[i] == NULL)
            {
              break;
            }
        }
    }

  if (swcr_sessions == NULL || i == swcr_sesnum)
    {
      if (swcr_sessions == NULL)
        {
          i = 1; /* We leave swcr_sessions[0] empty */
          swcr_sesnum = CRYPTO_SW_SESSIONS;
        }
      else
        {
          swcr_sesnum *= 2;
        }

      swd = kmm_calloc(swcr_sesnum, sizeof(struct swcr_data *));
      if (swd == NULL)
        {
          /* Reset session number */

          if (swcr_sesnum == CRYPTO_SW_SESSIONS)
            {
              swcr_sesnum = 0;
            }
          else
            {
              swcr_sesnum /= 2;
            }

          return 0;
        }

      /* Copy existing sessions */

      if (swcr_sessions)
        {
          bcopy(swcr_sessions, swd,
              (swcr_sesnum / 2) * sizeof(struct swcr_data *));
          kmm_free(swcr_sessions);
        }

      swcr_sessions = swd;
    }

  return i;
}

/* Generate a new software session. */

int swcr_newsession(FAR uint32_t *sid, FAR struct cryptoini *cri)
{
  FAR struct swcr_data **swd;
  FAR const struct auth_hash *axf;
  FAR const struct enc_xform *txf;
  FAR uint8_t *key = (FAR uint8_t *)cri->cri_key;
  int keylen = cri->cri_klen / 8;
  uint8_t hmackey[HMAC_MAX_BLOCK_LEN];
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
  FAR struct swkey_context_s *ctx = swkey_get_context();
  uint8_t keybuf[128];
#endif
  uint32_t i;
  int k;

  if (sid == NULL || cri == NULL)
    {
      return -EINVAL;
    }

  i = swcr_allocsid();
  if (i == 0)
    {
      return -ENOBUFS;
    }

  swd = &swcr_sessions[i];
  *sid = i;

  while (cri)
    {
      *swd = kmm_zalloc(sizeof(struct swcr_data));
      if (*swd == NULL)
        {
          swcr_freesession(i);
          return -ENOBUFS;
        }

      switch (cri->cri_alg)
        {
          case CRYPTO_3DES_CBC:
            txf = &enc_xform_3des;
            goto enccommon;
          case CRYPTO_BLF_CBC:
            txf = &enc_xform_blf;
            goto enccommon;
          case CRYPTO_CAST_CBC:
            txf = &enc_xform_cast5;
            goto enccommon;
          case CRYPTO_AES_CBC:
          case CRYPTO_AES_192_CBC:
          case CRYPTO_AES_256_CBC:
            txf = &enc_xform_aes;
            goto enccommon;
          case CRYPTO_AES_CTR:
            txf = &enc_xform_aes_ctr;
            goto enccommon;
          case CRYPTO_AES_XTS:
            txf = &enc_xform_aes_xts;
            goto enccommon;
          case CRYPTO_AES_GCM_16:
            txf = &enc_xform_aes_gcm;
            goto enccommon;
          case CRYPTO_AES_GMAC:
            txf = &enc_xform_aes_gmac;
            (*swd)->sw_exf = txf;
            break;
          case CRYPTO_AES_CMAC:
            txf = &enc_xform_aes_cmac;
            (*swd)->sw_exf = txf;
            break;
          case CRYPTO_AES_OFB:
            txf = &enc_xform_aes_ofb;
            goto enccommon;
          case CRYPTO_AES_CFB_8:
            txf = &enc_xform_aes_cfb_8;
            goto enccommon;
          case CRYPTO_AES_CFB_128:
            txf = &enc_xform_aes_cfb_128;
            goto enccommon;
          case CRYPTO_CHACHA20:
            txf = &enc_xform_chacha20;
            goto enccommon;
          case CRYPTO_CHACHA20_POLY1305:
            txf = &enc_xform_chacha20_poly1305;
            goto enccommon;
          case CRYPTO_NULL:
            txf = &enc_xform_null;
            goto enccommon;
          enccommon:
            if (txf->ctxsize > 0)
              {
                (*swd)->sw_kschedule = kmm_zalloc(txf->ctxsize);
                if ((*swd)->sw_kschedule == NULL)
                  {
                    swcr_freesession(i);
                    return -EINVAL;
                  }
              }

            if (cri->cri_flags & CRD_F_KEYID)
              {
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
                keylen = swkey_read(&ctx->file,
                                    *(FAR uint32_t *)cri->cri_key,
                                    keybuf, sizeof(keybuf));
                if (keylen < 0)
                  {
                    swcr_freesession(i);
                    return keylen;
                  }

                key = keybuf;
#endif
              }

            if (keylen > txf->maxkey ||
                keylen < txf->minkey)
              {
                swcr_freesession(i);
                return -EINVAL;
              }

            if (txf->setkey((*swd)->sw_kschedule, key, keylen) < 0)
              {
                swcr_freesession(i);
                return -EINVAL;
              }

            (*swd)->sw_exf = txf;
            break;

          case CRYPTO_MD5_HMAC:
            axf = &auth_hash_hmac_md5_96;
            goto authcommon;
          case CRYPTO_SHA1_HMAC:
          case CRYPTO_PBKDF2_HMAC_SHA1:
            axf = &auth_hash_hmac_sha1_96;
            goto authcommon;
          case CRYPTO_RIPEMD160_HMAC:
            axf = &auth_hash_hmac_ripemd_160_96;
            goto authcommon;
          case CRYPTO_SHA2_224_HMAC:
            axf = &auth_hash_hmac_sha2_224_114;
            goto authcommon;
          case CRYPTO_SHA2_256_HMAC:
          case CRYPTO_PBKDF2_HMAC_SHA256:
            axf = &auth_hash_hmac_sha2_256_128;
            goto authcommon;
          case CRYPTO_SHA2_384_HMAC:
            axf = &auth_hash_hmac_sha2_384_192;
            goto authcommon;
          case CRYPTO_SHA2_512_HMAC:
            axf = &auth_hash_hmac_sha2_512_256;
          authcommon:
            (*swd)->sw_ictx = kmm_malloc(axf->ctxsize);
            if ((*swd)->sw_ictx == NULL)
              {
                swcr_freesession(i);
                return -ENOBUFS;
              }

            (*swd)->sw_octx = kmm_malloc(axf->ctxsize);
            if ((*swd)->sw_octx == NULL)
              {
                swcr_freesession(i);
                return -ENOBUFS;
              }

            if (cri->cri_flags & CRD_F_KEYID)
              {
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
                keylen = swkey_read(&ctx->file,
                                    *(FAR uint32_t *)cri->cri_key,
                                    keybuf, sizeof(keybuf));
                if (keylen < 0)
                  {
                    swcr_freesession(i);
                    return keylen;
                  }

                key = keybuf;
#endif
              }

            /* Work on a local copy of the key so the caller's key
             * buffer (which may be read-only) is never modified by the
             * HMAC ipad/opad XOR operations. If the key is too long,
             * hash it first using ictx.
             */

            if (keylen > axf->keysize)
              {
                axf->init((*swd)->sw_ictx);
                axf->update((*swd)->sw_ictx, key, keylen);
                axf->final(hmackey, (*swd)->sw_ictx);
                keylen = axf->hashsize;
              }
            else
              {
                bcopy(key, hmackey, keylen);
              }

            for (k = 0; k < keylen; k++)
              {
                hmackey[k] ^= HMAC_IPAD_VAL;
              }

            axf->init((*swd)->sw_ictx);
            axf->update((*swd)->sw_ictx, hmackey, keylen);
            axf->update((*swd)->sw_ictx, hmac_ipad_buffer,
                        axf->blocksize - keylen);

            for (k = 0; k < keylen; k++)
              {
                hmackey[k] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);
              }

            axf->init((*swd)->sw_octx);
            axf->update((*swd)->sw_octx, hmackey, keylen);
            axf->update((*swd)->sw_octx, hmac_opad_buffer,
                        axf->blocksize - keylen);

            explicit_bzero(hmackey, keylen);

            (*swd)->sw_axf = axf;
            bcopy((*swd)->sw_ictx, &(*swd)->sw_ctx, axf->ctxsize);
            break;

          case CRYPTO_MD5:
            axf = &auth_hash_md5;
            goto auth3common;
          case CRYPTO_RIPEMD160:
            axf = &auth_hash_ripemd_160;
            goto auth3common;
          case CRYPTO_SHA1:
            axf = &auth_hash_sha1;
            goto auth3common;
          case CRYPTO_SHA2_224:
            axf = &auth_hash_sha2_224;
            goto auth3common;
          case CRYPTO_SHA2_256:
            axf = &auth_hash_sha2_256;
            goto auth3common;
          case CRYPTO_SHA2_384:
            axf = &auth_hash_sha2_384;
            goto auth3common;
          case CRYPTO_SHA2_512:
            axf = &auth_hash_sha2_512;

          auth3common:
            (*swd)->sw_ictx = kmm_zalloc(axf->ctxsize);
            if ((*swd)->sw_ictx == NULL)
              {
                swcr_freesession(i);
                return -ENOBUFS;
              }

            axf->init((*swd)->sw_ictx);
            (*swd)->sw_axf = axf;
            bcopy((*swd)->sw_ictx, &(*swd)->sw_ctx, axf->ctxsize);
            break;

          case CRYPTO_AES_128_GMAC:
            axf = &auth_hash_gmac_aes_128;
            goto auth4common;

          case CRYPTO_AES_192_GMAC:
            axf = &auth_hash_gmac_aes_192;
            goto auth4common;

          case CRYPTO_AES_256_GMAC:
            axf = &auth_hash_gmac_aes_256;
            goto auth4common;

          case CRYPTO_AES_128_CMAC:
            axf = &auth_hash_cmac_aes_128;
            goto auth4common;

          case CRYPTO_AES_256_CMAC:
            axf = &auth_hash_cmac_aes_256;
            goto auth4common;

          case CRYPTO_POLY1305:
            axf = &auth_hash_poly1305;
            goto auth4common;

          case CRYPTO_CRC8:
            axf = &auth_hash_crc8;
            goto auth4common;

          case CRYPTO_CRC16:
            axf = &auth_hash_crc16;
            goto auth4common;

          case CRYPTO_CRC32:
            axf = &auth_hash_crc32;
            goto auth4common;

          case CRYPTO_CHACHA20_POLY1305_MAC:
            axf = &auth_hash_chacha20_poly1305;

          auth4common:
            (*swd)->sw_ictx = kmm_malloc(axf->ctxsize);
            if ((*swd)->sw_ictx == NULL)
              {
                swcr_freesession(i);
                return -ENOBUFS;
              }

            if (cri->cri_flags & CRD_F_KEYID)
              {
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
                keylen = swkey_read(&ctx->file,
                                    *(FAR uint32_t *)cri->cri_key,
                                    keybuf, sizeof(keybuf));
                if (keylen < 0)
                  {
                    swcr_freesession(i);
                    return keylen;
                  }

                key = keybuf;
#endif
              }

            axf->init((*swd)->sw_ictx);
            axf->setkey((*swd)->sw_ictx, key, keylen);
            bcopy((*swd)->sw_ictx, &(*swd)->sw_ctx, axf->ctxsize);
            (*swd)->sw_axf = axf;
            break;

          case CRYPTO_ESN:

            /* nothing to do */

            break;
          default:
            swcr_freesession(i);
            return -EINVAL;
        }

      (*swd)->sw_alg = cri->cri_alg;
      cri = cri->cri_next;
      swd = &((*swd)->sw_next);
    }

  return 0;
}

/* Free a session. */

int swcr_freesession(uint64_t tid)
{
  FAR struct swcr_data *swd;
  FAR const struct enc_xform *txf;
  FAR const struct auth_hash *axf;
  uint32_t sid = ((uint32_t) tid) & 0xffffffff;

  if (sid > swcr_sesnum || swcr_sessions == NULL ||
      swcr_sessions[sid] == NULL)
    {
      return -EINVAL;
    }

  /* Silently accept and return */

  if (sid == 0)
    {
      return 0;
    }

  while ((swd = swcr_sessions[sid]) != NULL)
    {
      swcr_sessions[sid] = swd->sw_next;

      switch (swd->sw_alg)
        {
          case CRYPTO_3DES_CBC:
          case CRYPTO_BLF_CBC:
          case CRYPTO_CAST_CBC:
          case CRYPTO_RIJNDAEL128_CBC:
          case CRYPTO_AES_192_CBC:
          case CRYPTO_AES_256_CBC:
          case CRYPTO_AES_CTR:
          case CRYPTO_AES_XTS:
          case CRYPTO_AES_GCM_16:
          case CRYPTO_AES_GMAC:
          case CRYPTO_AES_CMAC:
          case CRYPTO_AES_OFB:
          case CRYPTO_AES_CFB_8:
          case CRYPTO_AES_CFB_128:
          case CRYPTO_CHACHA20:
          case CRYPTO_CHACHA20_POLY1305:
          case CRYPTO_NULL:
            txf = swd->sw_exf;

            if (swd->sw_kschedule)
            {
              explicit_bzero(swd->sw_kschedule, txf->ctxsize);
              kmm_free(swd->sw_kschedule);
            }

            break;

          case CRYPTO_MD5_HMAC:
          case CRYPTO_SHA1_HMAC:
          case CRYPTO_RIPEMD160_HMAC:
          case CRYPTO_SHA2_224_HMAC:
          case CRYPTO_SHA2_256_HMAC:
          case CRYPTO_SHA2_384_HMAC:
          case CRYPTO_SHA2_512_HMAC:
          case CRYPTO_PBKDF2_HMAC_SHA1:
          case CRYPTO_PBKDF2_HMAC_SHA256:
            axf = swd->sw_axf;

            if (swd->sw_ictx)
              {
                explicit_bzero(swd->sw_ictx, axf->ctxsize);
                kmm_free(swd->sw_ictx);
              }

            if (swd->sw_octx)
              {
                explicit_bzero(swd->sw_octx, axf->ctxsize);
                kmm_free(swd->sw_octx);
              }

            break;

          case CRYPTO_AES_128_GMAC:
          case CRYPTO_AES_192_GMAC:
          case CRYPTO_AES_256_GMAC:
          case CRYPTO_AES_128_CMAC:
          case CRYPTO_AES_256_CMAC:
          case CRYPTO_CHACHA20_POLY1305_MAC:
          case CRYPTO_MD5:
          case CRYPTO_POLY1305:
          case CRYPTO_RIPEMD160:
          case CRYPTO_SHA1:
          case CRYPTO_SHA2_224:
          case CRYPTO_SHA2_256:
          case CRYPTO_SHA2_384:
          case CRYPTO_SHA2_512:
          case CRYPTO_CRC8:
          case CRYPTO_CRC16:
          case CRYPTO_CRC32:
            axf = swd->sw_axf;

            if (swd->sw_ictx)
              {
                explicit_bzero(swd->sw_ictx, axf->ctxsize);
                kmm_free(swd->sw_ictx);
              }

            break;
          }

      kmm_free(swd);
    }

  return 0;
}

/* Deep-copy one session node; the xform/axf tables are shared constants. */

static int swcr_clonedata(FAR struct swcr_data *dst,
                          FAR const struct swcr_data *src)
{
  FAR const struct auth_hash *axf;
  FAR const struct enc_xform *txf;

  memcpy(dst, src, sizeof(*dst));
  dst->sw_next = NULL;

  switch (src->sw_alg)
    {
      case CRYPTO_3DES_CBC:
      case CRYPTO_BLF_CBC:
      case CRYPTO_CAST_CBC:
      case CRYPTO_RIJNDAEL128_CBC:
      case CRYPTO_AES_192_CBC:
      case CRYPTO_AES_256_CBC:
      case CRYPTO_AES_CTR:
      case CRYPTO_AES_XTS:
      case CRYPTO_AES_GCM_16:
      case CRYPTO_AES_GMAC:
      case CRYPTO_AES_CMAC:
      case CRYPTO_AES_OFB:
      case CRYPTO_AES_CFB_8:
      case CRYPTO_AES_CFB_128:
      case CRYPTO_CHACHA20:
      case CRYPTO_CHACHA20_POLY1305:
      case CRYPTO_NULL:
        txf = src->sw_exf;
        dst->sw_kschedule = NULL;
        if (src->sw_kschedule == NULL || txf->ctxsize == 0)
          {
            return OK;
          }

        dst->sw_kschedule = kmm_malloc(txf->ctxsize);
        if (dst->sw_kschedule == NULL)
          {
            return -ENOBUFS;
          }

        memcpy(dst->sw_kschedule, src->sw_kschedule, txf->ctxsize);
        return OK;

      case CRYPTO_MD5_HMAC:
      case CRYPTO_SHA1_HMAC:
      case CRYPTO_RIPEMD160_HMAC:
      case CRYPTO_SHA2_224_HMAC:
      case CRYPTO_SHA2_256_HMAC:
      case CRYPTO_SHA2_384_HMAC:
      case CRYPTO_SHA2_512_HMAC:
      case CRYPTO_PBKDF2_HMAC_SHA1:
      case CRYPTO_PBKDF2_HMAC_SHA256:
        axf = src->sw_axf;
        dst->sw_ictx = NULL;
        dst->sw_octx = kmm_malloc(axf->ctxsize);
        if (dst->sw_octx == NULL)
          {
            return -ENOBUFS;
          }

        memcpy(dst->sw_octx, src->sw_octx, axf->ctxsize);
        goto ictx;

      case CRYPTO_AES_128_GMAC:
      case CRYPTO_AES_192_GMAC:
      case CRYPTO_AES_256_GMAC:
      case CRYPTO_AES_128_CMAC:
      case CRYPTO_AES_256_CMAC:
      case CRYPTO_CHACHA20_POLY1305_MAC:
      case CRYPTO_MD5:
      case CRYPTO_POLY1305:
      case CRYPTO_RIPEMD160:
      case CRYPTO_SHA1:
      case CRYPTO_SHA2_224:
      case CRYPTO_SHA2_256:
      case CRYPTO_SHA2_384:
      case CRYPTO_SHA2_512:
      case CRYPTO_CRC8:
      case CRYPTO_CRC16:
      case CRYPTO_CRC32:
        axf = src->sw_axf;
        dst->sw_octx = NULL;

      ictx:
        dst->sw_ictx = kmm_malloc(axf->ctxsize);
        if (dst->sw_ictx == NULL)
          {
            return -ENOBUFS;
          }

        memcpy(dst->sw_ictx, src->sw_ictx, axf->ctxsize);
        return OK;

      default:
        return OK;
    }
}

/* Clone a running session, including any partially processed state. */

int swcr_clone(uint64_t tid, FAR uint32_t *sid)
{
  FAR const struct swcr_data *src;
  FAR struct swcr_data **dst;
  uint32_t old = (uint32_t)tid;
  uint32_t i;
  int ret;

  if (sid == NULL || old == 0 || old >= swcr_sesnum ||
      swcr_sessions == NULL || swcr_sessions[old] == NULL)
    {
      return -EINVAL;
    }

  i = swcr_allocsid();
  if (i == 0)
    {
      return -ENOBUFS;
    }

  dst = &swcr_sessions[i];
  for (src = swcr_sessions[old]; src; src = src->sw_next)
    {
      *dst = kmm_zalloc(sizeof(struct swcr_data));
      if (*dst == NULL)
        {
          swcr_freesession(i);
          return -ENOBUFS;
        }

      ret = swcr_clonedata(*dst, src);
      if (ret < 0)
        {
          swcr_freesession(i);
          return ret;
        }

      dst = &(*dst)->sw_next;
    }

  *sid = i;
  return OK;
}

/* Process a software request. */

int swcr_process(struct cryptop *crp)
{
  FAR const struct enc_xform *txf;
  FAR struct cryptodesc *crd;
  FAR struct swcr_data *sw;
  uint32_t lid;

  /* Sanity check */

  if (crp == NULL)
    {
      return -EINVAL;
    }

  if (crp->crp_desc == NULL)
    {
      crp->crp_etype = -EINVAL;
      goto done;
    }

  lid = crp->crp_sid & 0xffffffff;
  if (lid >= swcr_sesnum || lid == 0 || swcr_sessions[lid] == NULL)
    {
      crp->crp_etype = -ENOENT;
      goto done;
    }

  /* Go through crypto descriptors, processing as we go */

  for (crd = crp->crp_desc; crd; crd = crd->crd_next)
    {
      /* Find the crypto context.
       * XXX Note that the logic here prevents us from having
       * XXX the same algorithm multiple times in a session
       * XXX (or rather, we can but it won't give us the right
       * XXX results). To do that, we'd need some way of differentiating
       * XXX between the various instances of an algorithm (so we can
       * XXX locate the correct crypto context).
       */

      for (sw = swcr_sessions[lid];
           sw && sw->sw_alg != crd->crd_alg;
           sw = sw->sw_next);

      /* No such context ? */

      if (sw == NULL)
        {
          crp->crp_etype = -EINVAL;
          goto done;
        }

      switch (sw->sw_alg)
        {
          case CRYPTO_NULL:
            {
              break;
            }

          case CRYPTO_3DES_CBC:
          case CRYPTO_BLF_CBC:
          case CRYPTO_CAST_CBC:
          case CRYPTO_RIJNDAEL128_CBC:
          case CRYPTO_AES_192_CBC:
          case CRYPTO_AES_256_CBC:
          case CRYPTO_AES_CTR:
          case CRYPTO_AES_XTS:
          case CRYPTO_AES_OFB:
          case CRYPTO_AES_CFB_8:
          case CRYPTO_AES_CFB_128:
          case CRYPTO_CHACHA20:
            txf = sw->sw_exf;

            if (crp->crp_iv)
              {
                if (!(crd->crd_flags & CRD_F_IV_EXPLICIT))
                  {
                    bcopy(crp->crp_iv, crd->crd_iv, txf->ivsize);
                    crd->crd_flags |= CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
                    crd->crd_skip = 0;
                  }
              }
            else
              {
                crd->crd_flags |= CRD_F_IV_PRESENT;
                crd->crd_skip = txf->blocksize;
                crd->crd_len -= txf->blocksize;
              }

            if ((crp->crp_etype = swcr_encdec(crp, crd, sw,
                crp->crp_buf)) != 0)
              {
                goto done;
              }

            break;
          case CRYPTO_MD5_HMAC:
          case CRYPTO_SHA1_HMAC:
          case CRYPTO_RIPEMD160_HMAC:
          case CRYPTO_SHA2_224_HMAC:
          case CRYPTO_SHA2_256_HMAC:
          case CRYPTO_SHA2_384_HMAC:
          case CRYPTO_SHA2_512_HMAC:
            if ((crp->crp_etype = swcr_authcompute(crp, crd, sw,
                crp->crp_buf)) != 0)
              {
                goto done;
              }

            break;

          case CRYPTO_PBKDF2_HMAC_SHA1:
          case CRYPTO_PBKDF2_HMAC_SHA256:
            swcr_pbkdf2(crp, crd, sw, crp->crp_buf);
            break;

          case CRYPTO_MD5:
          case CRYPTO_POLY1305:
          case CRYPTO_RIPEMD160:
          case CRYPTO_SHA1:
          case CRYPTO_SHA2_224:
          case CRYPTO_SHA2_256:
          case CRYPTO_SHA2_384:
          case CRYPTO_SHA2_512:
          case CRYPTO_CRC8:
          case CRYPTO_CRC16:
          case CRYPTO_CRC32:
            if ((crp->crp_etype = swcr_hash(crp, crd, sw,
                crp->crp_buf)) != 0)
              {
                goto done;
              }

            break;

          case CRYPTO_AES_GCM_16:
          case CRYPTO_AES_GMAC:
          case CRYPTO_AES_128_GMAC:
          case CRYPTO_AES_192_GMAC:
          case CRYPTO_AES_256_GMAC:
          case CRYPTO_AES_128_CMAC:
          case CRYPTO_AES_256_CMAC:
          case CRYPTO_CHACHA20_POLY1305:
          case CRYPTO_CHACHA20_POLY1305_MAC:
            crp->crp_etype = swcr_authenc(crp);
            goto done;
            break;

          default:

            /* Unknown/unsupported algorithm */

            crp->crp_etype = -EINVAL;
            goto done;
        }
    }

done:
  return 0;
}

/* Compute one HMAC using the precomputed inner/outer pad contexts stored in
 * the session (sw_ictx / sw_octx). PBKDF2 keeps the HMAC key (the password)
 * constant across all iterations, so the pad contexts are set up once during
 * newsession and only the per-iteration message is fed here.
 */

static void swcr_pbkdf2_hmac(FAR struct swcr_data *sw,
                             FAR const uint8_t *msg, size_t msglen,
                             FAR uint8_t *out)
{
  FAR const struct auth_hash *axf = sw->sw_axf;

  /* inner = H((key ^ ipad) || msg) */

  memcpy(&sw->sw_ctx, sw->sw_ictx, axf->ctxsize);
  axf->update(&sw->sw_ctx, msg, msglen);
  axf->final(out, &sw->sw_ctx);

  /* out = H((key ^ opad) || inner) */

  memcpy(&sw->sw_ctx, sw->sw_octx, axf->ctxsize);
  axf->update(&sw->sw_ctx, out, axf->hashsize);
  axf->final(out, &sw->sw_ctx);
}

int swcr_pbkdf2(FAR struct cryptop *crp,
                FAR struct cryptodesc *crd,
                FAR struct swcr_data *sw,
                caddr_t buf)
{
  FAR const struct auth_hash *axf = sw->sw_axf;
  uint8_t saltblk[HMAC_MAX_BLOCK_LEN + AALG_MAX_RESULT_LEN + 4];
  uint8_t U[AALG_MAX_RESULT_LEN];
  uint8_t T[AALG_MAX_RESULT_LEN];
  size_t generated = 0;
  uint32_t blocknum;
  uint32_t i;
  uint32_t j;

  if (sw->sw_ictx == NULL || sw->sw_octx == NULL || crp->crp_iter < 1)
    {
      return -EINVAL;
    }

  if (crp->crp_ilen + 4 > (int)sizeof(saltblk))
    {
      return -EINVAL;
    }

  for (blocknum = 1; generated < crp->crp_olen; blocknum++)
    {
      /* U1 = HMAC(password, salt || INT_32_BE(blocknum)) */

      memcpy(saltblk, crp->crp_buf, crp->crp_ilen);
      *(FAR uint32_t *)(saltblk + crp->crp_ilen) = htobe32(blocknum);
      swcr_pbkdf2_hmac(sw, saltblk, crp->crp_ilen + 4, U);
      memcpy(T, U, axf->hashsize);

      /* U2..Uc = HMAC(password, U(i-1)); T = U1 ^ U2 ^ ... ^ Uc */

      for (i = 1; i < (uint32_t)crp->crp_iter; i++)
        {
          swcr_pbkdf2_hmac(sw, U, axf->hashsize, U);
          for (j = 0; j < axf->hashsize; j++)
            {
              T[j] ^= U[j];
            }
        }

      size_t tocopy = MIN(crp->crp_olen - generated, axf->hashsize);
      memcpy((FAR uint8_t *)crp->crp_mac + generated, T, tocopy);
      generated += tocopy;
    }

  return 0;
}

static void swcr_rsa_reverse(FAR uint8_t *buf, size_t len)
{
  uint8_t tmp;
  size_t i;

  for (i = 0; i < len / 2; i++)
    {
      tmp = buf[i];
      buf[i] = buf[len - 1 - i];
      buf[len - 1 - i] = tmp;
    }
}

/* Load the required krp_param[slot] into comp[slot].  Returns OK, -EINVAL
 * when the caller left it empty, or another negated errno.
 */

static int swcr_rsa_load(FAR struct cryptkop *krp,
                         FAR struct swcr_rsa_s *rsa, int slot)
{
  FAR struct crparam *param = &krp->krp_param[slot];
  FAR uint8_t *buf = rsa->comp[slot];
  int len;

  if (param->crp_p == NULL || param->crp_nbits == 0)
    {
      return -EINVAL;
    }

  if (krp->krp_flags & CRD_F_KEYID)
    {
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
      len = swkey_read(&swkey_get_context()->file,
                       *(FAR uint32_t *)param->crp_p, buf, RSA_MAX_BYTES);
#else
      len = -ENOTSUP;
#endif
      if (len < 0)
        {
          return len;
        }
    }
  else
    {
      len = param->crp_nbits / 8;
      if (len <= RSA_MAX_BYTES)
        {
          memcpy(buf, param->crp_p, len);
        }
    }

  /* swkey_read() reports the stored size, which can exceed what it copied */

  if (len == 0)
    {
      return -EINVAL;
    }
  else if (len > RSA_MAX_BYTES)
    {
      return -ENOBUFS;
    }

  swcr_rsa_reverse(buf, len);   /* cryptodev is little-endian */
  rsa->len[slot] = len;
  return OK;
}

static int swcr_rsa_load_public(FAR struct cryptkop *krp,
                                FAR struct swcr_rsa_s *rsa)
{
  static const uint8_t default_e[] =
  {
    0x01, 0x00, 0x01
  };

  int ret;

  ret = swcr_rsa_load(krp, rsa, SWCR_RSA_N);
  if (ret < 0)
    {
      return ret;
    }

  /* A key-id public key may leave e out and mean 65537 */

  if (krp->krp_param[SWCR_RSA_E].crp_nbits == 0 &&
      (krp->krp_flags & CRD_F_KEYID))
    {
      memcpy(rsa->comp[SWCR_RSA_E], default_e, sizeof(default_e));
      rsa->len[SWCR_RSA_E] = sizeof(default_e);
      return OK;
    }

  return swcr_rsa_load(krp, rsa, SWCR_RSA_E);
}

/* The caller selects CRT by passing dp, as rpmsg and vhost do when they
 * pick CRK_MOD_EXP_CRT; every component of the chosen form is required.
 */

static int swcr_rsa_load_private(FAR struct cryptkop *krp,
                                 FAR struct swcr_rsa_s *rsa)
{
  int ret;

  ret = swcr_rsa_load(krp, rsa, SWCR_RSA_N);
  if (ret < 0)
    {
      return ret;
    }

  if (krp->krp_param[SWCR_RSA_DP].crp_nbits == 0)
    {
      return swcr_rsa_load(krp, rsa, SWCR_RSA_D);
    }

  ret = swcr_rsa_load(krp, rsa, SWCR_RSA_P);
  if (ret < 0)
    {
      return ret;
    }

  ret = swcr_rsa_load(krp, rsa, SWCR_RSA_Q);
  if (ret < 0)
    {
      return ret;
    }

  ret = swcr_rsa_load(krp, rsa, SWCR_RSA_DP);
  if (ret < 0)
    {
      return ret;
    }

  ret = swcr_rsa_load(krp, rsa, SWCR_RSA_DQ);
  if (ret < 0)
    {
      return ret;
    }

  return swcr_rsa_load(krp, rsa, SWCR_RSA_QP);
}

/* Run the public or private primitive on the little-endian integer in[]
 * and store the low outlen bytes of the result, little-endian, in out[];
 * in and out may alias.  Returns the modulus length or a negated errno.
 */

static int swcr_rsa_run(FAR struct cryptkop *krp, bool private,
                        FAR const uint8_t *in, size_t inlen,
                        FAR uint8_t *out, size_t outlen)
{
  FAR struct swcr_rsa_s *rsa;
  FAR uint8_t *x;
  struct rsa_key_s key;
  size_t modlen;
  size_t i;
  int ret;

  rsa = kmm_zalloc(sizeof(*rsa));
  if (rsa == NULL)
    {
      return -ENOMEM;
    }

  if (private)
    {
      ret = swcr_rsa_load_private(krp, rsa);
    }
  else
    {
      ret = swcr_rsa_load_public(krp, rsa);
    }

  if (ret < 0)
    {
      goto out;
    }

  key.n = rsa->comp[SWCR_RSA_N];
  key.nlen = rsa->len[SWCR_RSA_N];
  key.e = rsa->comp[SWCR_RSA_E];
  key.elen = rsa->len[SWCR_RSA_E];
  key.d = rsa->comp[SWCR_RSA_D];
  key.dlen = rsa->len[SWCR_RSA_D];
  key.p = rsa->comp[SWCR_RSA_P];
  key.plen = rsa->len[SWCR_RSA_P];
  key.q = rsa->comp[SWCR_RSA_Q];
  key.qlen = rsa->len[SWCR_RSA_Q];
  key.dp = rsa->comp[SWCR_RSA_DP];
  key.dplen = rsa->len[SWCR_RSA_DP];
  key.dq = rsa->comp[SWCR_RSA_DQ];
  key.dqlen = rsa->len[SWCR_RSA_DQ];
  key.qp = rsa->comp[SWCR_RSA_QP];
  key.qplen = rsa->len[SWCR_RSA_QP];

  ret = -EINVAL;
  modlen = rsa_modulus_len(key.n, key.nlen);
  if (modlen == 0)
    {
      goto out;
    }

  /* Little-endian in[] to big-endian x; bytes above modlen must be 0 */

  for (i = modlen; i < inlen; i++)
    {
      if (in[i] != 0)
        {
          goto out;
        }
    }

  x = rsa->comp[SWCR_RSA_X];
  memcpy(x, in, MIN(inlen, modlen));
  swcr_rsa_reverse(x, modlen);

  if (private)
    {
      ret = rsa_private(x, modlen, &key);
    }
  else
    {
      ret = rsa_public(x, modlen, &key);
    }

  if (ret < 0)
    {
      goto out;
    }

  swcr_rsa_reverse(x, modlen);
  memcpy(out, x, MIN(outlen, modlen));
  if (outlen > modlen)
    {
      memset(out + modlen, 0, outlen - modlen);
    }

  ret = modlen;

out:
  explicit_bzero(rsa, sizeof(*rsa));
  kmm_free(rsa);
  return ret;
}

static int swcr_rsa_public(FAR struct cryptkop *krp,
                           FAR const uint8_t *in, size_t inlen,
                           FAR uint8_t *out, size_t outlen)
{
  return swcr_rsa_run(krp, false, in, inlen, out, outlen);
}

static int swcr_rsa_private(FAR struct cryptkop *krp,
                            FAR const uint8_t *in, size_t inlen,
                            FAR uint8_t *out, size_t outlen)
{
  return swcr_rsa_run(krp, true, in, inlen, out, outlen);
}

static int swcr_mod_exp(FAR struct cryptkop *krp)
{
  FAR struct crparam *src = &krp->krp_param[0];
  FAR struct crparam *dst = &krp->krp_param[krp->krp_iparams +
                                             krp->krp_oparams - 1];
  int ret;

  ret = swcr_rsa_public(krp, (FAR uint8_t *)src->crp_p,
                        src->crp_nbits / 8, (FAR uint8_t *)dst->crp_p,
                        dst->crp_nbits / 8);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

static int swcr_mod_exp_crt(FAR struct cryptkop *krp)
{
  FAR struct crparam *src = &krp->krp_param[0];
  FAR struct crparam *dst = &krp->krp_param[krp->krp_iparams +
                                             krp->krp_oparams - 1];
  int ret;

  ret = swcr_rsa_private(krp, (FAR uint8_t *)src->crp_p,
                         src->crp_nbits / 8, (FAR uint8_t *)dst->crp_p,
                         dst->crp_nbits / 8);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

/* EMSA-PKCS1-v1_5 with SHA-256 (RFC 8017 9.2), stored byte-reversed like
 * every other RSA integer cryptodev carries; hash[] is reversed as well.
 */

static int swcr_rsa_pkcs15_encode(FAR uint8_t *em, size_t emlen,
                                  FAR const uint8_t *hash, size_t hashlen)
{
  static const uint8_t digestinfo[] =
  {
    0x20, 0x04, 0x00, 0x05, 0x01, 0x02, 0x04, 0x03, 0x65, 0x01,
    0x48, 0x86, 0x60, 0x09, 0x06, 0x0d, 0x30, 0x31, 0x30
  };

  size_t pslen;

  if (hash == NULL || hashlen != SHA256_DIGEST_LENGTH ||
      emlen < hashlen + sizeof(digestinfo) + 11)
    {
      return -EINVAL;
    }

  pslen = emlen - hashlen - sizeof(digestinfo) - 3;
  memcpy(em, hash, hashlen);
  em += hashlen;
  memcpy(em, digestinfo, sizeof(digestinfo));
  em += sizeof(digestinfo);
  *em++ = 0x00;
  memset(em, 0xff, pslen);
  em += pslen;
  *em++ = 0x01;
  *em = 0x00;
  return OK;
}

static int swcr_rsa_pkcs15_sign(FAR struct cryptkop *krp)
{
  FAR struct crparam *src;
  FAR struct crparam *dst;
  FAR uint8_t *em;
  size_t len;
  int ret;

  src = &krp->krp_param[0];
  dst = &krp->krp_param[krp->krp_iparams + krp->krp_oparams - 1];
  em = (FAR uint8_t *)dst->crp_p;
  len = dst->crp_nbits / 8;
  ret = swcr_rsa_pkcs15_encode(em, len, (FAR uint8_t *)src->crp_p,
                               src->crp_nbits / 8);
  if (ret < 0)
    {
      return ret;
    }

  ret = swcr_rsa_private(krp, em, len, em, len);
  if (ret < 0)
    {
      return ret;
    }

  if (ret != len)
    {
      return -EINVAL;
    }

  return OK;
}

/* RSASSA-PSS with SHA-256, MGF1 and a 32-byte salt (RFC 8017 8.1).  The
 * encoding runs on big-endian strings as the RFC defines it; mHash comes
 * in byte-reversed and EM is reversed before the private operation.
 */

static int swcr_rsa_pss_sign(FAR struct cryptkop *krp)
{
  uint8_t m_prime[8 + 2 * SHA256_DIGEST_LENGTH];
  uint8_t hash[SHA256_DIGEST_LENGTH];
  FAR const uint8_t *mhash;
  FAR struct crparam *src;
  FAR struct crparam *dst;
  FAR uint8_t *salt;
  FAR uint8_t *em;
  size_t emlen;
  size_t dblen;
  size_t hlen;
  size_t i;
  SHA2_CTX ctx;
  int ret;

  src = &krp->krp_param[0];
  dst = &krp->krp_param[krp->krp_iparams + krp->krp_oparams - 1];
  mhash = (FAR const uint8_t *)src->crp_p;
  em = (FAR uint8_t *)dst->crp_p;
  hlen = SHA256_DIGEST_LENGTH;
  emlen = dst->crp_nbits / 8;
  if (mhash == NULL || src->crp_nbits / 8 != hlen ||
      emlen < 2 * hlen + 2 || emlen > RSA_MAX_BYTES)
    {
      return -EINVAL;
    }

  /* H = Hash(0x00 * 8 || mHash || salt) */

  memset(m_prime, 0, 8);
  for (i = 0; i < hlen; i++)
    {
      m_prime[8 + i] = mhash[hlen - 1 - i];
    }

  salt = m_prime + 8 + hlen;
  arc4random_buf(salt, hlen);
  sha256init(&ctx);
  sha256update(&ctx, m_prime, sizeof(m_prime));
  sha256final(hash, &ctx);

  /* EM = (MGF1(H) ^ (PS || 0x01 || salt)) || H || 0xbc */

  dblen = emlen - hlen - 1;
  sha256_mgf1(hash, hlen, em, dblen);
  em[dblen - hlen - 1] ^= 0x01;
  for (i = 0; i < hlen; i++)
    {
      em[dblen - hlen + i] ^= salt[i];
    }

  em[0] &= 0x7f;
  memcpy(em + dblen, hash, hlen);
  em[emlen - 1] = 0xbc;

  swcr_rsa_reverse(em, emlen);
  ret = swcr_rsa_private(krp, em, emlen, em, emlen);
  explicit_bzero(m_prime, sizeof(m_prime));
  if (ret < 0)
    {
      return ret;
    }

  if (ret != emlen)
    {
      return -EINVAL;
    }

  return OK;
}

static int swcr_dh_make_public(FAR struct cryptkop *krp)
{
  /* Curve25519 is used for testing. In fact,
   * the four parameters of this interface are p, g, x, gx；
   * p: used to determine the conic curve;
   * g: the base point of the curve;
   * x: the private key produced by random;
   * gx: the public key generated by the private key,
   *  which could be caculated by gx = g ^ x mod p;
   * In curve25519, p and g are fixed.
   */

  uint8_t *secret = (uint8_t *)krp->krp_param[2].crp_p;
  uint8_t *public = (uint8_t *)krp->krp_param[3].crp_p;

  curve25519_generate_secret(secret);
  return curve25519_generate_public(public, secret);
}

static int swcr_dh_make_common(FAR struct cryptkop *krp)
{
  /* Curve25519 is used for testing. In fact,
   * the four parameters of this interface are:
   * public key / private key / p (the conic curve) / shared key
   */

  uint8_t *public = (uint8_t *)krp->krp_param[0].crp_p;
  uint8_t *secret = (uint8_t *)krp->krp_param[1].crp_p;
  uint8_t *shared = (uint8_t *)krp->krp_param[3].crp_p;
  return curve25519(shared, secret, public);
}

static int swcr_rsa_pss_verify(FAR struct cryptkop *krp)
{
  uint8_t m_prime[8 + 2 * SHA256_DIGEST_LENGTH];
  uint8_t hash[SHA256_DIGEST_LENGTH];
  FAR const uint8_t *mhash;
  FAR struct crparam *sig;
  FAR struct crparam *msg;
  FAR uint8_t *em;
  FAR uint8_t *db;
  size_t emlen;
  size_t dblen;
  size_t hlen;
  size_t i;
  SHA2_CTX ctx;
  int ret;

  sig = &krp->krp_param[0];
  msg = &krp->krp_param[3];
  mhash = (FAR const uint8_t *)msg->crp_p;
  hlen = SHA256_DIGEST_LENGTH;
  if (mhash == NULL || msg->crp_nbits / 8 != hlen)
    {
      return -EINVAL;
    }

  em = kmm_malloc(2 * RSA_MAX_BYTES);
  if (em == NULL)
    {
      return -ENOMEM;
    }

  ret = swcr_rsa_public(krp, (FAR uint8_t *)sig->crp_p,
                        sig->crp_nbits / 8, em, RSA_MAX_BYTES);
  if (ret < 0)
    {
      goto out;
    }

  emlen = ret;
  ret = -EINVAL;
  if (emlen < 2 * hlen + 2)
    {
      goto out;
    }

  swcr_rsa_reverse(em, emlen);
  if (em[emlen - 1] != 0xbc || (em[0] & 0x80) != 0)
    {
      goto out;
    }

  /* DB = maskedDB ^ MGF1(H) must be PS || 0x01 || salt */

  dblen = emlen - hlen - 1;
  db = em + RSA_MAX_BYTES;
  sha256_mgf1(em + dblen, hlen, db, dblen);
  for (i = 0; i < dblen; i++)
    {
      db[i] ^= em[i];
    }

  db[0] &= 0x7f;
  for (i = 0; i < dblen - hlen - 1; i++)
    {
      if (db[i] != 0)
        {
          goto out;
        }
    }

  if (db[dblen - hlen - 1] != 0x01)
    {
      goto out;
    }

  memset(m_prime, 0, 8);
  for (i = 0; i < hlen; i++)
    {
      m_prime[8 + i] = mhash[hlen - 1 - i];
    }

  memcpy(m_prime + 8 + hlen, db + dblen - hlen, hlen);
  sha256init(&ctx);
  sha256update(&ctx, m_prime, sizeof(m_prime));
  sha256final(hash, &ctx);

  if (timingsafe_bcmp(hash, em + dblen, hlen) == 0)
    {
      ret = OK;
    }

out:
  kmm_free(em);
  return ret;
}

/* With padding given and padding || hash spanning the modulus, the
 * caller supplied the whole encoded message; otherwise the standard
 * EMSA-PKCS1-v1_5 encoding of hash is expected.
 */

static int swcr_rsa_verify(FAR struct cryptkop *krp)
{
  FAR struct crparam *sig;
  FAR struct crparam *pad;
  FAR struct crparam *msg;
  FAR uint8_t *expect;
  FAR uint8_t *em;
  size_t hashlen;
  size_t padlen;
  size_t len;
  int ret;

  sig = &krp->krp_param[0];
  pad = &krp->krp_param[3];
  msg = &krp->krp_param[4];
  hashlen = msg->crp_nbits / 8;
  padlen = pad->crp_nbits / 8;
  em = kmm_malloc(2 * RSA_MAX_BYTES);
  if (em == NULL)
    {
      return -ENOMEM;
    }

  ret = swcr_rsa_public(krp, (FAR uint8_t *)sig->crp_p,
                        sig->crp_nbits / 8, em, RSA_MAX_BYTES);
  if (ret < 0)
    {
      goto out;
    }

  len = ret;
  expect = em + RSA_MAX_BYTES;
  if (pad->crp_p != NULL && msg->crp_p != NULL && padlen + hashlen == len)
    {
      memcpy(expect, msg->crp_p, hashlen);
      memcpy(expect + hashlen, pad->crp_p, padlen);
      ret = OK;
    }
  else
    {
      ret = swcr_rsa_pkcs15_encode(expect, len, (FAR uint8_t *)msg->crp_p,
                                   hashlen);
    }

  if (ret == OK)
    {
      if (timingsafe_bcmp(em, expect, len) != 0)
        {
          ret = -EINVAL;
        }
    }

out:
  kmm_free(em);
  return ret;
}

static int swcr_ecc256_genkey(FAR struct cryptkop *krp)
{
  uint8_t d[secp256r1];
  uint8_t x[secp256r1];
  uint8_t y[secp256r1];

  if (ecc_make_key_uncomp(x, y, d) == 0)
    {
      return -EINVAL;
    }

  memcpy(krp->krp_param[0].crp_p, d, secp256r1);
  memcpy(krp->krp_param[1].crp_p, x, secp256r1);
  memcpy(krp->krp_param[2].crp_p, y, secp256r1);
  return OK;
}

static int swcr_ecc256_sign(struct cryptkop *krp)
{
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
  FAR struct swkey_context_s *ctx = swkey_get_context();
#endif
  FAR uint8_t *hash = (uint8_t *)krp->krp_param[1].crp_p;
  uint8_t sig[secp256r1 * 2];
  uint8_t d[secp256r1];

  if (krp->krp_flags & CRD_F_KEYID)
    {
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
      int length;
      length = swkey_read(&ctx->file,
                          *(FAR uint32_t *)krp->krp_param[0].crp_p,
                          d, secp256r1);
      if (length < 0)
        {
          return length;
        }
#else
      return -ENOSYS;
#endif
    }
  else
    {
      memcpy(d, krp->krp_param[0].crp_p,
             krp->krp_param[0].crp_nbits / 8);
    }

  if (ecdsa_sign(d, hash, sig) == 0)
    {
      return -EINVAL;
    }

  memcpy(krp->krp_param[2].crp_p, sig, secp256r1);
  memcpy(krp->krp_param[3].crp_p, sig + secp256r1, secp256r1);
  return OK;
}

static int swcr_ecc256_verify(struct cryptkop *krp)
{
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
  FAR struct swkey_context_s *ctx = swkey_get_context();
#endif
  uint8_t publickey[secp256r1 + 1];
  uint8_t signature[secp256r1 * 2];
  uint8_t point[secp256r1 * 2];
  uint8_t hash[secp256r1];
  FAR uint8_t *x;
  FAR uint8_t *y;

  if (krp->krp_flags & CRD_F_KEYID)
    {
#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
      int length;
      length = swkey_read(&ctx->file,
                          *(FAR uint32_t *)krp->krp_param[0].crp_p,
                          point, secp256r1 * 2);
      if (length < 0)
        {
          return length;
        }
#else
      return -ENOSYS;
#endif
    }
  else
    {
      memcpy(point, krp->krp_param[0].crp_p,
             krp->krp_param[0].crp_nbits / 8);
      memcpy(point + secp256r1, krp->krp_param[1].crp_p,
             krp->krp_param[1].crp_nbits / 8);
    }

  /* Compressed public key */

  x = point;
  y = point + secp256r1;
  publickey[0] = 2 + (y[secp256r1 - 1] & 0x01);
  memcpy(publickey + 1, x, secp256r1);

  /* Padding signature and result */

  memcpy(signature, krp->krp_param[3].crp_p,
                    krp->krp_param[3].crp_nbits / 8);
  memcpy(signature + secp256r1, krp->krp_param[4].crp_p,
                                krp->krp_param[4].crp_nbits / 8);
  memcpy(hash, krp->krp_param[5].crp_p, krp->krp_param[5].crp_nbits / 8);
  return ecdsa_verify(publickey, hash, signature) == 0;
}

static int swcr_ecdh256_gen_public(FAR struct cryptkop *krp)
{
  /* Generating an ephemeral ECDH keypair is identical to ECDSA key
   * generation: both produce a SECP256R1 keypair in param[0]=d,
   * param[1]=x, param[2]=y.
   */

  return swcr_ecc256_genkey(krp);
}

static int swcr_ecdh256_compute_shared(FAR struct cryptkop *krp)
{
  uint8_t publickey[secp256r1 + 1];
  uint8_t privatekey[secp256r1];
  uint8_t secret[secp256r1];
  FAR uint8_t *x;
  FAR uint8_t *y;

  /* Private key: param[0]; peer public key: param[1]=Q.X, param[2]=Q.Y. */

  x = (FAR uint8_t *)krp->krp_param[1].crp_p;
  y = (FAR uint8_t *)krp->krp_param[2].crp_p;

  /* Compress the peer public key: SEC1 prefix (0x02/0x03 by Y parity) + X. */

  publickey[0] = 2 + (y[secp256r1 - 1] & 0x01);
  memcpy(publickey + 1, x, secp256r1);

  memcpy(privatekey, krp->krp_param[0].crp_p,
                     krp->krp_param[0].crp_nbits / 8);

  if (ecdh_shared_secret(publickey, privatekey, secret) == 0)
    {
      return -EINVAL;
    }

  memcpy(krp->krp_param[3].crp_p, secret, secp256r1);
  return OK;
}

int swcr_kprocess(struct cryptkop *krp)
{
  /* Sanity check */

  if (krp == NULL)
    {
      return -EINVAL;
    }

  /* Go through crypto descriptors, processing as we go */

  switch (krp->krp_op)
    {
      case CRK_MOD_EXP:
        if ((krp->krp_status = swcr_mod_exp(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_MOD_EXP_CRT:
        if ((krp->krp_status = swcr_mod_exp_crt(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_RSA_PKCS15_SIGN:
        if ((krp->krp_status = swcr_rsa_pkcs15_sign(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_RSA_PSS_SIGN:
        if ((krp->krp_status = swcr_rsa_pss_sign(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_DH_MAKE_PUBLIC:
        if ((krp->krp_status = swcr_dh_make_public(krp) != 0))
          {
            goto done;
          }

        break;
      case CRK_DH_COMPUTE_KEY:
        if ((krp->krp_status = swcr_dh_make_common(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_RSA_PKCS15_VERIFY:
        if ((krp->krp_status = swcr_rsa_verify(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_RSA_PSS_VERIFY:
        if ((krp->krp_status = swcr_rsa_pss_verify(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_ECDSA_SECP256R1_SIGN:
        if ((krp->krp_status = swcr_ecc256_sign(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_ECDSA_SECP256R1_VERIFY:
        if ((krp->krp_status = swcr_ecc256_verify(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_ECDSA_SECP256R1_GENKEY:
        if ((krp->krp_status = swcr_ecc256_genkey(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_ECDH_GEN_PUBLIC:
        if ((krp->krp_status = swcr_ecdh256_gen_public(krp)) != 0)
          {
            goto done;
          }

        break;
      case CRK_ECDH_COMPUTE_SHARED:
        if ((krp->krp_status = swcr_ecdh256_compute_shared(krp)) != 0)
          {
            goto done;
          }

        break;
      default:

        /* Unknown/unsupported algorithm */

        krp->krp_status = -EINVAL;
        goto done;
    }

done:
  return 0;
}

/* Initialize the driver, called from the kernel main(). */

void swcr_init(void)
{
  int algs[CRYPTO_ALGORITHM_MAX + 1];
  int kalgs[CRK_ALGORITHM_MAX + 1];
  int flags = CRYPTOCAP_F_SOFTWARE | CRYPTOCAP_F_ENCRYPT_MAC |
              CRYPTOCAP_F_MAC_ENCRYPT;

  swcr_id = crypto_get_driverid(flags);
  if (swcr_id < 0)
    {
      /* This should never happen */

      PANIC();
    }

  memset(algs, 0, sizeof(algs));
  algs[CRYPTO_3DES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_BLF_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_CAST_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_MD5_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA1_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_RIPEMD160_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_RIJNDAEL128_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_192_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_256_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_CTR] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_XTS] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_GCM_16] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_NULL] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA2_224_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA2_256_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA2_384_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA2_512_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_128_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_192_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_256_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_OFB] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_CFB_8] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_CFB_128] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_CHACHA20] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_CHACHA20_POLY1305] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_CHACHA20_POLY1305_MAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_MD5] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_POLY1305] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_RIPEMD160] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA1] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA2_224] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA2_256] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA2_384] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_SHA2_512] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_CRC8] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_CRC16] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_CRC32] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_CMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_128_CMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_AES_256_CMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_PBKDF2_HMAC_SHA1] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_PBKDF2_HMAC_SHA256] = CRYPTO_ALG_FLAG_SUPPORTED;
  algs[CRYPTO_ESN] = CRYPTO_ALG_FLAG_SUPPORTED;

  crypto_register2(swcr_id, algs, swcr_newsession,
                   swcr_freesession, swcr_process, swcr_clone);

  memset(kalgs, 0, sizeof(kalgs));
  kalgs[CRK_MOD_EXP] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_MOD_EXP_CRT] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_DH_MAKE_PUBLIC] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_DH_COMPUTE_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_RSA_PKCS15_SIGN] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_RSA_PKCS15_VERIFY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_RSA_PSS_SIGN] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_RSA_PSS_VERIFY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_ECDSA_SECP256R1_SIGN] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_ECDSA_SECP256R1_VERIFY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_ECDSA_SECP256R1_GENKEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_ECDH_GEN_PUBLIC] = CRYPTO_ALG_FLAG_SUPPORTED;
  kalgs[CRK_ECDH_COMPUTE_SHARED] = CRYPTO_ALG_FLAG_SUPPORTED;
  crypto_kregister(swcr_id, kalgs, swcr_kprocess);
}

#endif /* CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO */
