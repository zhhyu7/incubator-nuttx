/****************************************************************************
 * include/net/cfg80211.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_NET_CFG80211_H
#define __INCLUDE_NET_CFG80211_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <netinet/if_ether.h>
#include <net/nl80211.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/bits.h>
#include <nuttx/compiler.h>
#include <nuttx/kmalloc.h>
#include <nuttx/list.h>
#include <nuttx/mutex.h>
#include <nuttx/wqueue.h>
#include <nuttx/wireless/ieee80211/ieee80211.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* cfg80211 is the configuration API for 802.11 devices. It bridges
 * userspace and drivers, and offers some utility functionality associated
 * with 802.11. cfg80211 must, directly or indirectly via mac80211, be used
 * by all modern wireless drivers, so that they offer a consistent API
 * through nl80211. For backward compatibility, cfg80211 also offers
 * wireless extensions to userspace, but hides them from drivers completely.
 *
 * Additionally, cfg80211 contains code to help enforce regulatory spectrum
 * use restrictions.
 */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* In order for a driver to use cfg80211, it must register the hardware
 * device with cfg80211. This happens through a number of hardware
 * capability structs described below.
 *
 * The fundamental structure for each device is the 'wiphy', of which each
 * instance describes a physical wireless device connected to the system.
 * Each such wiphy can have zero, one, or many virtual interfaces
 * associated with it, which need to be identified as such by pointing
 * the network interface's @ieee80211_ptr pointer to a &struct wireless_dev
 * which further describes the wireless part of the interface, normally
 * this struct is embedded in the network interface's private data area.
 * Drivers can optionally allow creating or destroying virtual interfaces
 * on the fly, but without at least one or the ability to create some
 * the wireless device isn't useful.
 *
 * Each wiphy structure contains device capability information, and also
 * has a pointer to the various operations the driver offers. The
 * definitions and structures here describe these capabilities in detail.
 */

/* Wireless hardware capability structures */

struct wiphy;

/* Channel flags set by the regulatory control code */

/**
 * enum ieee80211_channel_flags - channel flags
 *
 * Channel flags set by the regulatory control code.
 *
 * @IEEE80211_CHAN_DISABLED: This channel is disabled.
 * @IEEE80211_CHAN_NO_IR: do not initiate radiation, this includes
 *  sending probe requests or beaconing.
 * @IEEE80211_CHAN_PSD: power spectral density regulatory limits apply.
 * @IEEE80211_CHAN_RADAR: Radar detection is required on this channel.
 * @IEEE80211_CHAN_NO_HT40PLUS: extension channel above this channel
 *  is not permitted.
 * @IEEE80211_CHAN_NO_HT40MINUS: extension channel below this channel
 *  is not permitted.
 * @IEEE80211_CHAN_NO_OFDM: OFDM is not allowed on this channel.
 * @IEEE80211_CHAN_NO_80MHZ: 80 MHz bandwidth is not permitted.
 * @IEEE80211_CHAN_NO_160MHZ: 160 MHz bandwidth is not permitted.
 * @IEEE80211_CHAN_INDOOR_ONLY: see %NL80211_FREQUENCY_ATTR_INDOOR_ONLY.
 * @IEEE80211_CHAN_IR_CONCURRENT: see
 *  %NL80211_FREQUENCY_ATTR_IR_CONCURRENT.
 * @IEEE80211_CHAN_NO_20MHZ: 20 MHz bandwidth is not permitted.
 * @IEEE80211_CHAN_NO_10MHZ: 10 MHz bandwidth is not permitted.
 * @IEEE80211_CHAN_NO_HE: HE operation is not permitted.
 * @IEEE80211_CHAN_NO_UHR: UHR operation is not permitted.
 * @IEEE80211_CHAN_NO_320MHZ: 320 MHz bandwidth is not permitted.
 * @IEEE80211_CHAN_NO_EHT: EHT operation is not permitted.
 * @IEEE80211_CHAN_DFS_CONCURRENT: DFS can be used concurrently.
 * @IEEE80211_CHAN_NO_6GHZ_VLP_CLIENT: 6 GHz VLP clients are not permitted.
 * @IEEE80211_CHAN_NO_6GHZ_AFC_CLIENT: 6 GHz AFC clients are not permitted.
 * @IEEE80211_CHAN_CAN_MONITOR: monitor mode is permitted.
 * @IEEE80211_CHAN_ALLOW_6GHZ_VLP_AP: 6 GHz VLP AP operation is permitted.
 * @IEEE80211_CHAN_ALLOW_20MHZ_ACTIVITY: 20 MHz activity is permitted.
 * @IEEE80211_CHAN_S1G_NO_PRIMARY: channel cannot be an S1G primary channel.
 * @IEEE80211_CHAN_NO_4MHZ: 4 MHz bandwidth is not permitted.
 * @IEEE80211_CHAN_NO_8MHZ: 8 MHz bandwidth is not permitted.
 * @IEEE80211_CHAN_NO_16MHZ: 16 MHz bandwidth is not permitted.
 */

enum ieee80211_channel_flags
{
  IEEE80211_CHAN_DISABLED             = BIT(0),
  IEEE80211_CHAN_NO_IR                = BIT(1),
  IEEE80211_CHAN_PSD                  = BIT(2),
  IEEE80211_CHAN_RADAR                = BIT(3),
  IEEE80211_CHAN_NO_HT40PLUS          = BIT(4),
  IEEE80211_CHAN_NO_HT40MINUS         = BIT(5),
  IEEE80211_CHAN_NO_OFDM              = BIT(6),
  IEEE80211_CHAN_NO_80MHZ             = BIT(7),
  IEEE80211_CHAN_NO_160MHZ            = BIT(8),
  IEEE80211_CHAN_INDOOR_ONLY          = BIT(9),
  IEEE80211_CHAN_IR_CONCURRENT        = BIT(10),
  IEEE80211_CHAN_NO_20MHZ             = BIT(11),
  IEEE80211_CHAN_NO_10MHZ             = BIT(12),
  IEEE80211_CHAN_NO_HE                = BIT(13),
  IEEE80211_CHAN_NO_UHR               = BIT(18),
  IEEE80211_CHAN_NO_320MHZ            = BIT(19),
  IEEE80211_CHAN_NO_EHT               = BIT(20),
  IEEE80211_CHAN_DFS_CONCURRENT       = BIT(21),
  IEEE80211_CHAN_NO_6GHZ_VLP_CLIENT   = BIT(22),
  IEEE80211_CHAN_NO_6GHZ_AFC_CLIENT   = BIT(23),
  IEEE80211_CHAN_CAN_MONITOR          = BIT(24),
  IEEE80211_CHAN_ALLOW_6GHZ_VLP_AP    = BIT(25),
  IEEE80211_CHAN_ALLOW_20MHZ_ACTIVITY = BIT(26),
  IEEE80211_CHAN_S1G_NO_PRIMARY       = BIT(27),
  IEEE80211_CHAN_NO_4MHZ              = BIT(28),
  IEEE80211_CHAN_NO_8MHZ              = BIT(29),
  IEEE80211_CHAN_NO_16MHZ             = BIT(30),
};

/**
 * struct ieee80211_channel - channel definition
 *
 * This structure describes a single channel for use
 * with cfg80211.
 *
 * @center_freq: center frequency in MHz
 * @freq_offset: offset from @center_freq, in KHz
 * @hw_value: hardware-specific value for the channel
 * @flags: channel flags from &enum ieee80211_channel_flags.
 * @orig_flags: channel flags at registration time, used by regulatory
 *  code to support devices with additional restrictions
 * @band: band this channel belongs to.
 * @max_antenna_gain: maximum antenna gain in dBi
 * @max_power: maximum transmission power (in dBm)
 * @max_reg_power: maximum regulatory transmission power (in dBm)
 * @beacon_found: helper to regulatory code to indicate when a beacon
 *  has been found on this channel. Use regulatory_hint_found_beacon()
 *  to enable this, this is useful only on 5 GHz band.
 * @orig_mag: internal use
 * @orig_mpwr: internal use
 * @dfs_state: current state of this channel. Only relevant if radar is
 *  required on this channel.
 * @dfs_state_entered: timestamp (jiffies) when the dfs state was entered.
 * @dfs_cac_ms: DFS CAC time in milliseconds, this is valid for DFS channels.
 */

struct ieee80211_channel
{
  enum nl80211_band band;
  uint32_t center_freq;
  uint16_t freq_offset;
  uint16_t hw_value;
  uint32_t flags;
  int max_antenna_gain;
  int max_power;
  int max_reg_power;
  bool beacon_found;
  uint32_t orig_flags;
  int orig_mag;
  int orig_mpwr;
  enum nl80211_dfs_state dfs_state;
  unsigned long dfs_state_entered;
  unsigned int dfs_cac_ms;
};

/**
 * enum ieee80211_rate_flags - rate flags
 *
 * Hardware/specification flags for rates. These are structured
 * in a way that allows using the same bitrate structure for
 * different bands/PHY modes.
 *
 * @IEEE80211_RATE_SHORT_PREAMBLE: Hardware can send with short
 *  preamble on this bitrate; only relevant in 2.4GHz band and
 *  with CCK rates.
 * @IEEE80211_RATE_MANDATORY_A: This bitrate is a mandatory rate
 *  when used with 802.11a (on the 5 GHz band); filled by the
 *  core code when registering the wiphy.
 * @IEEE80211_RATE_MANDATORY_B: This bitrate is a mandatory rate
 *  when used with 802.11b (on the 2.4 GHz band); filled by the
 *  core code when registering the wiphy.
 * @IEEE80211_RATE_MANDATORY_G: This bitrate is a mandatory rate
 *  when used with 802.11g (on the 2.4 GHz band); filled by the
 *  core code when registering the wiphy.
 * @IEEE80211_RATE_ERP_G: This is an ERP rate in 802.11g mode.
 * @IEEE80211_RATE_SUPPORTS_5MHZ: Rate can be used in 5 MHz mode
 * @IEEE80211_RATE_SUPPORTS_10MHZ: Rate can be used in 10 MHz mode
 */

enum ieee80211_rate_flags
{
  IEEE80211_RATE_SHORT_PREAMBLE = 1 << 0,
  IEEE80211_RATE_MANDATORY_A    = 1 << 1,
  IEEE80211_RATE_MANDATORY_B    = 1 << 2,
  IEEE80211_RATE_MANDATORY_G    = 1 << 3,
  IEEE80211_RATE_ERP_G          = 1 << 4,
  IEEE80211_RATE_SUPPORTS_5MHZ  = 1 << 5,
  IEEE80211_RATE_SUPPORTS_10MHZ = 1 << 6
};

/**
 * enum ieee80211_privacy - BSS privacy filter
 *
 * @IEEE80211_PRIVACY_ON: privacy bit set
 * @IEEE80211_PRIVACY_OFF: privacy bit clear
 * @IEEE80211_PRIVACY_ANY: Wildcard value for matching any privacy setting
 */

enum ieee80211_privacy
{
  IEEE80211_PRIVACY_ON,
  IEEE80211_PRIVACY_OFF,
  IEEE80211_PRIVACY_ANY
};

#define IEEE80211_PRIVACY(x) \
  ((x) ? IEEE80211_PRIVACY_ON : IEEE80211_PRIVACY_OFF)

/**
 * struct ieee80211_rate - bitrate definition
 *
 * This structure describes a bitrate that an 802.11 PHY can
 * operate with. The two values @hw_value and @hw_value_short
 * are only for driver use when pointers to this structure are
 * passed around.
 *
 * @flags: rate-specific flags
 * @bitrate: bitrate in units of 100 Kbps
 * @hw_value: driver/hardware value for this rate
 * @hw_value_short: driver/hardware value for this rate when
 *  short preamble is used
 */

struct ieee80211_rate
{
  uint32_t flags;
  uint16_t bitrate;
  uint16_t hw_value;
  uint16_t hw_value_short;
};

/**
 * struct ieee80211_he_obss_pd - AP settings for spatial reuse
 *
 * @enable: is the feature enabled.
 * @sr_ctrl: The SR Control field of SRP element.
 * @non_srg_max_offset: non-SRG maximum tx power offset
 * @min_offset: minimal tx power offset an associated station shall use
 * @max_offset: maximum tx power offset an associated station shall use
 * @bss_color_bitmap: bitmap that indicates the BSS color values used by
 *  members of the SRG
 * @partial_bssid_bitmap: bitmap that indicates the partial BSSID values
 *  used by members of the SRG
 */

struct ieee80211_he_obss_pd
{
  bool enable;
  uint8_t sr_ctrl;
  uint8_t non_srg_max_offset;
  uint8_t min_offset;
  uint8_t max_offset;
  uint8_t bss_color_bitmap[8];
  uint8_t partial_bssid_bitmap[8];
};

/**
 * struct cfg80211_he_bss_color - AP settings for BSS coloring
 *
 * @color: the current color.
 * @enabled: HE BSS color is used
 * @partial: define the AID equation.
 */

struct cfg80211_he_bss_color
{
  uint8_t color;
  bool enabled;
  bool partial;
};

/**
 * struct ieee80211_sta_ht_cap - STA's HT capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11n HT capabilities for an STA.
 *
 * @ht_supported: is HT supported by the STA
 * @cap: HT capabilities map as described in 802.11n spec
 * @ampdu_factor: Maximum A-MPDU length factor
 * @ampdu_density: Minimum A-MPDU spacing
 * @mcs: Supported MCS rates
 */

struct ieee80211_sta_ht_cap
{
  uint16_t cap;
  bool ht_supported;
  uint8_t ampdu_factor;
  uint8_t ampdu_density;
  struct ieee80211_mcs_info mcs;
};

/**
 * struct ieee80211_sta_vht_cap - STA's VHT capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ac VHT capabilities for an STA.
 *
 * @vht_supported: is VHT supported by the STA
 * @cap: VHT capabilities map as described in 802.11ac spec
 * @vht_mcs: Supported VHT MCS rates
 */

struct ieee80211_sta_vht_cap
{
  bool vht_supported;
  uint32_t cap;
  struct ieee80211_vht_mcs_info vht_mcs;
};

#define IEEE80211_HE_PPE_THRES_MAX_LEN  25

/**
 * struct ieee80211_sta_he_cap - STA's HE capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ax HE capabilities for a STA.
 *
 * @has_he: true iff HE data is valid.
 * @he_cap_elem: Fixed portion of the HE capabilities element.
 * @he_mcs_nss_supp: The supported NSS/MCS combinations.
 * @ppe_thres: Holds the PPE Thresholds data.
 */

struct ieee80211_sta_he_cap
{
  bool has_he;
  struct ieee80211_he_cap_elem he_cap_elem;
  struct ieee80211_he_mcs_nss_supp he_mcs_nss_supp;
  uint8_t ppe_thres[IEEE80211_HE_PPE_THRES_MAX_LEN];
};

/**
 * struct ieee80211_eht_mcs_nss_supp - EHT max supported NSS per MCS
 *
 * See P802.11be_D1.3 Table 9-401k - "Subfields of the Supported EHT-MCS
 * and NSS Set field"
 *
 * @only_20mhz: MCS/NSS support for 20 MHz-only STA.
 * @bw: MCS/NSS support for 80, 160 and 320 MHz
 * @bw._80: MCS/NSS support for BW <= 80 MHz
 * @bw._160: MCS/NSS support for BW = 160 MHz
 * @bw._320: MCS/NSS support for BW = 320 MHz
 */

begin_packed_struct struct ieee80211_eht_mcs_nss_supp
{
  union
  {
    struct ieee80211_eht_mcs_nss_supp_20mhz_only only_20mhz;
    struct
    {
      struct ieee80211_eht_mcs_nss_supp_bw _80;
      struct ieee80211_eht_mcs_nss_supp_bw _160;
      struct ieee80211_eht_mcs_nss_supp_bw _320;
    } bw;
  };
} end_packed_struct;

#define IEEE80211_EHT_PPE_THRES_MAX_LEN  32

/**
 * struct ieee80211_sta_eht_cap - STA's EHT capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11be EHT capabilities for a STA.
 *
 * @has_eht: true iff EHT data is valid.
 * @eht_cap_elem: Fixed portion of the eht capabilities element.
 * @eht_mcs_nss_supp: The supported NSS/MCS combinations.
 * @eht_ppe_thres: Holds the PPE Thresholds data.
 */

struct ieee80211_sta_eht_cap
{
  bool has_eht;
  struct ieee80211_eht_cap_elem_fixed eht_cap_elem;
  struct ieee80211_eht_mcs_nss_supp eht_mcs_nss_supp;
  uint8_t eht_ppe_thres[IEEE80211_EHT_PPE_THRES_MAX_LEN];
};

/**
 * struct ieee80211_sband_iftype_data - sband data per interface type
 *
 * This structure encapsulates sband data that is relevant for the
 * interface types defined in @types_mask.  Each type in the
 * @types_mask must be unique across all instances of iftype_data.
 *
 * @types_mask: interface types mask
 * @he_cap: holds the HE capabilities
 * @he_6ghz_capa: HE 6 GHz capabilities, must be filled in for a
 *  6 GHz band channel (and 0 may be valid value).
 * @eht_cap: STA's EHT capabilities
 * @vendor_elems: vendor element(s) to advertise
 * @vendor_elems.data: vendor element(s) data
 * @vendor_elems.len: vendor element(s) length
 */

struct ieee80211_sband_iftype_data
{
  uint16_t types_mask;
  struct ieee80211_sta_he_cap he_cap;
  struct ieee80211_he_6ghz_capa he_6ghz_capa;
  struct ieee80211_sta_eht_cap eht_cap;
  struct
  {
    const uint8_t *data;
    unsigned int len;
  } vendor_elems;
};

/**
 * enum ieee80211_edmg_bw_config - allowed channel bandwidth configurations
 *
 * @IEEE80211_EDMG_BW_CONFIG_4: 2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_5: 2.16GHz and 4.32GHz
 * @IEEE80211_EDMG_BW_CONFIG_6: 2.16GHz, 4.32GHz and 6.48GHz
 * @IEEE80211_EDMG_BW_CONFIG_7: 2.16GHz, 4.32GHz, 6.48GHz and 8.64GHz
 * @IEEE80211_EDMG_BW_CONFIG_8: 2.16GHz and 2.16GHz + 2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_9: 2.16GHz, 4.32GHz and 2.16GHz + 2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_10: 2.16GHz, 4.32GHz, 6.48GHz and
 *  2.16GHz+2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_11: 2.16GHz, 4.32GHz, 6.48GHz, 8.64GHz and
 *  2.16GHz+2.16GHz
 * @IEEE80211_EDMG_BW_CONFIG_12: 2.16GHz, 2.16GHz + 2.16GHz and
 *  4.32GHz + 4.32GHz
 * @IEEE80211_EDMG_BW_CONFIG_13: 2.16GHz, 4.32GHz, 2.16GHz + 2.16GHz and
 *  4.32GHz + 4.32GHz
 * @IEEE80211_EDMG_BW_CONFIG_14: 2.16GHz, 4.32GHz, 6.48GHz, 2.16GHz + 2.16GHz
 *  4.32GHz + 4.32GHz
 * @IEEE80211_EDMG_BW_CONFIG_15: 2.16GHz, 4.32GHz, 6.48GHz, 8.64GHz,
 *  2.16GHz + 2.16GHz and 4.32GHz + 4.32GHz
 */

enum ieee80211_edmg_bw_config
{
  IEEE80211_EDMG_BW_CONFIG_4  = 4,
  IEEE80211_EDMG_BW_CONFIG_5  = 5,
  IEEE80211_EDMG_BW_CONFIG_6  = 6,
  IEEE80211_EDMG_BW_CONFIG_7  = 7,
  IEEE80211_EDMG_BW_CONFIG_8  = 8,
  IEEE80211_EDMG_BW_CONFIG_9  = 9,
  IEEE80211_EDMG_BW_CONFIG_10 = 10,
  IEEE80211_EDMG_BW_CONFIG_11 = 11,
  IEEE80211_EDMG_BW_CONFIG_12 = 12,
  IEEE80211_EDMG_BW_CONFIG_13 = 13,
  IEEE80211_EDMG_BW_CONFIG_14 = 14,
  IEEE80211_EDMG_BW_CONFIG_15 = 15,
};

/**
 * struct ieee80211_edmg - EDMG configuration
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ay EDMG configuration
 *
 * @channels: bitmap that indicates the 2.16 GHz channel(s)
 *  that are allowed to be used for transmissions.
 *  Bit 0 indicates channel 1, bit 1 indicates channel 2, etc.
 *  Set to 0 indicate EDMG not supported.
 * @bw_config: Channel BW Configuration subfield encodes
 *  the allowed channel bandwidth configurations
 */

struct ieee80211_edmg
{
  uint8_t channels;
  enum ieee80211_edmg_bw_config bw_config;
};

/**
 * struct ieee80211_sta_s1g_cap - STA's S1G capabilities
 *
 * This structure describes most essential parameters needed
 * to describe 802.11ah S1G capabilities for a STA.
 *
 * @s1g: is STA an S1G STA
 * @cap: S1G capabilities information
 * @nss_mcs: Supported NSS MCS set
 */

struct ieee80211_sta_s1g_cap
{
  bool s1g;
  uint8_t cap[10];
  uint8_t nss_mcs[5];
};

/**
 * struct ieee80211_supported_band - frequency band definition
 *
 * This structure describes a frequency band a wiphy
 * is able to operate in.
 *
 * @channels: Array of channels the hardware can operate with
 *  in this band.
 * @band: the band this structure represents
 * @n_channels: Number of channels in @channels
 * @bitrates: Array of bitrates the hardware can operate with
 *  in this band. Must be sorted to give a valid "supported
 *  rates" IE, i.e. CCK rates first, then OFDM.
 * @n_bitrates: Number of bitrates in @bitrates
 * @ht_cap: HT capabilities in this band
 * @vht_cap: VHT capabilities in this band
 * @s1g_cap: S1G capabilities in this band
 * @edmg_cap: EDMG capabilities in this band
 * @s1g_cap: S1G capabilities in this band (S1B band only, of course)
 * @n_iftype_data: number of iftype data entries
 * @iftype_data: interface type data entries.  Note that the bits in
 *  @types_mask inside this structure cannot overlap (i.e. only
 *  one occurrence of each type is allowed across all instances of
 *  @iftype_data).
 */

struct ieee80211_supported_band
{
  struct ieee80211_channel *channels;
  struct ieee80211_rate *bitrates;
  enum nl80211_band band;
  int n_channels;
  int n_bitrates;
  struct ieee80211_sta_ht_cap ht_cap;
  struct ieee80211_sta_vht_cap vht_cap;
  struct ieee80211_sta_s1g_cap s1g_cap;
  struct ieee80211_edmg edmg_cap;
  uint16_t n_iftype_data;
  const struct ieee80211_sband_iftype_data *iftype_data;
};

/**
 * struct vif_params - describes virtual interface parameters
 * @flags: monitor interface flags, unchanged if 0, otherwise
 *  %MONITOR_FLAG_CHANGED will be set
 * @use_4addr: use 4-address frames
 * @macaddr: address to use for this virtual interface.
 *  If this parameter is set to zero address the driver may
 *  determine the address as needed.
 *  This feature is only fully supported by drivers that enable the
 *  %NL80211_FEATURE_MAC_ON_CREATE flag.  Others may support creating
 *  only p2p devices with specified MAC.
 * @vht_mumimo_groups: MU-MIMO groupID, used for monitoring MU-MIMO packets
 *  belonging to that MU-MIMO groupID; %NULL if not changed
 * @vht_mumimo_follow_addr: MU-MIMO follow address, used for monitoring
 *  MU-MIMO packets going to the specified station; %NULL if not changed
 */

struct vif_params
{
  uint32_t flags;
  int use_4addr;
  uint8_t macaddr[ETH_ALEN];
  const uint8_t *vht_mumimo_groups;
  const uint8_t *vht_mumimo_follow_addr;
};

/**
 * struct key_params - key information
 *
 * Information about a key
 *
 * @key: key material
 * @key_len: length of key material
 * @cipher: cipher suite selector
 * @seq: sequence counter (IV/PN) for TKIP and CCMP keys, only used
 *  with the get_key() callback, must be in little endian,
 *  length given by @seq_len.
 * @seq_len: length of @seq.
 * @vlan_id: vlan_id for VLAN group key (if nonzero)
 * @mode: key install mode (RX_TX, NO_TX or SET_TX)
 */

struct key_params
{
  const uint8_t *key;
  const uint8_t *seq;
  int key_len;
  int seq_len;
  uint16_t vlan_id;
  uint32_t cipher;
  enum nl80211_key_mode mode;
};

/**
 * struct cfg80211_chan_def - channel definition
 * @chan: the (control) channel
 * @width: channel width
 * @center_freq1: center frequency of first segment
 * @center_freq2: center frequency of second segment
 *  (only with 80+80 MHz)
 * @edmg: define the EDMG channels configuration.
 *  If edmg is requested (i.e. the .channels member is non-zero),
 *  chan will define the primary channel and all other
 *  parameters are ignored.
 * @freq1_offset: offset from @center_freq1, in KHz
 */

struct cfg80211_chan_def
{
  struct ieee80211_channel *chan;
  enum nl80211_chan_width width;
  uint32_t center_freq1;
  uint32_t center_freq2;
  struct ieee80211_edmg edmg;
  uint16_t freq1_offset;
};

/**
 * struct cfg80211_bitrate_control
 *
 * @legacy: legacy bitrates mask
 * @ht_mcs: HT MCS rates mask
 * @vht_mcs: VHT MCS rates mask
 * @he_mcs: HE MCS rates mask
 * @eht_mcs: EHT MCS rates mask
 * @gi: guard interval setting
 * @he_gi: HE guard interval setting
 * @eht_gi: EHT guard interval setting
 * @he_ltf: HE LTF setting
 * @eht_ltf: EHT LTF setting
 * @nonht_dup_6ghz: enable non-HT duplicate frames for 6 GHz
 */

struct cfg80211_bitrate_control
{
  uint32_t legacy;
  uint8_t ht_mcs[IEEE80211_HT_MCS_MASK_LEN];
  uint16_t vht_mcs[NL80211_VHT_NSS_MAX];
  uint16_t he_mcs[NL80211_HE_NSS_MAX];
  uint16_t eht_mcs[NL80211_EHT_NSS_MAX];
  enum nl80211_txrate_gi gi;
  enum nl80211_he_gi he_gi;
  enum nl80211_eht_gi eht_gi;
  enum nl80211_he_ltf he_ltf;
  enum nl80211_eht_ltf eht_ltf;
  bool nonht_dup_6ghz;
};

/**
 * struct cfg80211_bitrate_mask - masks for bitrate control
 *
 * Used to configure bitrate masks for different bands.
 *
 * @control: array of bitrate control structures for each band
 */

struct cfg80211_bitrate_mask
{
  struct cfg80211_bitrate_control control[NUM_NL80211_BANDS];
};

#define CFG80211_MAX_WEP_KEYS         4
#define CFG80211_MAX_NUM_AKM_SUITES  10

/**
 * struct cfg80211_crypto_settings - Crypto settings
 * @wpa_versions: indicates which, if any, WPA versions are enabled
 *  (from enum nl80211_wpa_versions)
 * @cipher_group: group key cipher suite (or 0 if unset)
 * @n_ciphers_pairwise: number of AP supported unicast ciphers
 * @ciphers_pairwise: unicast key cipher suites
 * @n_akm_suites: number of AKM suites
 * @akm_suites: AKM suites
 * @control_port: Whether user space controls IEEE 802.1X port, i.e.,
 *  whether the driver is allowed to assume that the port is unauthorized
 *  until authorized by user space. If true, the driver is required to
 *  assume that the port is unauthorized until authorized by user space.
 *  Otherwise, port is marked authorized by default.
 * @control_port_ethertype: the control port protocol that should be
 *  allowed through even on unauthorized ports
 * @control_port_no_encrypt: TRUE to prevent encryption of control port
 *  protocol frames.
 * @control_port_over_nl80211: TRUE if userspace expects to exchange control
 *  port frames over NL80211 instead of the network interface.
 * @control_port_no_preauth: disables pre-auth rx over the nl80211 control
 *  port for mac80211
 * @wep_keys: static WEP keys, if not NULL points to an array of
 *  CFG80211_MAX_WEP_KEYS WEP keys
 * @wep_tx_key: key index (0..3) of the default TX static WEP key
 * @psk: PSK (for devices supporting 4-way-handshake offload)
 * @sae_pwd: password for SAE authentication (for devices supporting SAE
 *  offload)
 * @sae_pwd_len: length of SAE password (for devices supporting SAE offload)
 * @sae_pwe: The mechanisms allowed for SAE PWE derivation:
 *
 *  NL80211_SAE_PWE_UNSPECIFIED
 *    Not-specified, used to indicate userspace did not specify any
 *    preference. The driver should follow its internal policy in
 *    such a scenario.
 *
 *  NL80211_SAE_PWE_HUNT_AND_PECK
 *    Allow hunting-and-pecking loop only
 *
 *  NL80211_SAE_PWE_HASH_TO_ELEMENT
 *    Allow hash-to-element only
 *
 *  NL80211_SAE_PWE_BOTH
 *    Allow either hunting-and-pecking loop or hash-to-element
 */

struct cfg80211_crypto_settings
{
  uint32_t wpa_versions;
  uint32_t cipher_group;
  int n_ciphers_pairwise;
  uint32_t ciphers_pairwise[NL80211_MAX_NR_CIPHER_SUITES];
  int n_akm_suites;
  uint32_t akm_suites[CFG80211_MAX_NUM_AKM_SUITES];
  bool control_port;
  uint16_t control_port_ethertype;
  bool control_port_no_encrypt;
  bool control_port_over_nl80211;
  bool control_port_no_preauth;
  struct key_params *wep_keys;
  int wep_tx_key;
  const uint8_t *psk;
  const uint8_t *sae_pwd;
  uint8_t sae_pwd_len;
  enum nl80211_sae_pwe_mechanism sae_pwe;
};

/**
 * struct cfg80211_mbssid_config - AP settings for multi bssid
 *
 * @tx_wdev: pointer to the transmitted interface in the MBSSID set
 * @index: index of this AP in the multi bssid group.
 * @ema: set to true if the beacons should be sent out in EMA mode.
 */

struct cfg80211_mbssid_config
{
  struct wireless_dev *tx_wdev;
  uint8_t index;
  bool ema;
};

/**
 * struct cfg80211_mbssid_elem - BSSID element
 *
 * @data: Data for multiple BSSID elements
 * @len: Length of data
 */

struct cfg80211_mbssid_elem
{
  const uint8_t *data;
  size_t len;
};

/**
 * struct cfg80211_mbssid_elems - Multiple BSSID elements
 *
 * Used to configure multiple BSSID elements for beacon frames.
 *
 * @cnt: Number of elements in array @elem
 * @elem: Array of multiple BSSID element(s) to be added into Beacon frames
 */

struct cfg80211_mbssid_elems
{
  uint8_t cnt;
  struct cfg80211_mbssid_elem elem[];
};

/**
 * struct cfg80211_beacon_data - beacon data
 *
 * Used to configure beacon frame data for AP interfaces.
 *
 * @link_id: the link ID for the AP MLD link sending this beacon
 * @head: head portion of beacon (before TIM IE) or NULL if not changed
 * @tail: tail portion of beacon (after TIM IE) or NULL if not changed
 * @beacon_ies: extra information element(s) to add into Beacon frames
 *   or NULL
 * @proberesp_ies: extra information element(s) to add into Probe Response
 *   frames or NULL
 * @assocresp_ies: extra information element(s) to add into (Re)Association
 *   Response frames or NULL
 * @probe_resp: probe response template (AP mode only) or NULL
 * @lci: Measurement Report element content, starting with Measurement Token
 *   (measurement type 8) or NULL
 * @civicloc: Measurement Report element content, starting with Measurement
 *   Token (measurement type 11) or NULL
 * @mbssid_ies: multiple BSSID elements
 * @ftm_responder: enable FTM responder functionality; -1 for no change
 * @head_len: length of @head
 * @tail_len: length of @tail
 * @beacon_ies_len: length of beacon_ies in octets
 * @proberesp_ies_len: length of proberesp_ies in octets
 * @assocresp_ies_len: length of assocresp_ies in octets
 * @probe_resp_len: length of probe response template (@probe_resp)
 * @lci_len: LCI data length
 * @civicloc_len: Civic location data length
 * @he_bss_color_valid: indicates whether bss color attribute is present
 *   in beacon data or not
 */

struct cfg80211_beacon_data
{
  unsigned int link_id;

  const uint8_t *head;
  const uint8_t *tail;
  const uint8_t *beacon_ies;
  const uint8_t *proberesp_ies;
  const uint8_t *assocresp_ies;
  const uint8_t *probe_resp;
  const uint8_t *lci;
  const uint8_t *civicloc;
  struct cfg80211_mbssid_elems *mbssid_ies;
  int8_t ftm_responder;

  size_t head_len;
  size_t tail_len;
  size_t beacon_ies_len;
  size_t proberesp_ies_len;
  size_t assocresp_ies_len;
  size_t probe_resp_len;
  size_t lci_len;
  size_t civicloc_len;
  bool he_bss_color_valid;
};

struct mac_address
{
  uint8_t addr[ETH_ALEN];
};

/**
 * struct cfg80211_acl_data - Access control list data
 *
 * @acl_policy: ACL policy to be applied on the station's
 *  entry specified by mac_addr
 * @n_acl_entries: Number of MAC address entries passed
 * @mac_addrs: List of MAC addresses of stations to be used for ACL
 */

struct cfg80211_acl_data
{
  enum nl80211_acl_policy acl_policy;
  int n_acl_entries;

  /* Keep it last */

  struct mac_address mac_addrs[];
};

/**
 * struct cfg80211_fils_discovery - FILS discovery parameters from
 * IEEE Std 802.11ai-2016, Annex C.3 MIB detail.
 *
 * @min_interval: Minimum packet interval in TUs (0 - 10000)
 * @max_interval: Maximum packet interval in TUs (0 - 10000)
 * @tmpl_len: Template length
 * @tmpl: Template data for FILS discovery frame including the action
 *  frame headers.
 */

struct cfg80211_fils_discovery
{
  uint32_t min_interval;
  uint32_t max_interval;
  size_t tmpl_len;
  const uint8_t *tmpl;
};

/**
 * struct cfg80211_unsol_bcast_probe_resp - Unsolicited broadcast probe
 *  response parameters in 6GHz.
 *
 * @interval: Packet interval in TUs. Maximum allowed is 20 TU, as mentioned
 *  in IEEE P802.11ax/D6.0 26.17.2.3.2 - AP behavior for fast passive
 *  scanning
 * @tmpl_len: Template length
 * @tmpl: Template data for probe response
 */

struct cfg80211_unsol_bcast_probe_resp
{
  uint32_t interval;
  size_t tmpl_len;
  const uint8_t *tmpl;
};

/**
 * struct cfg80211_ap_settings - AP configuration
 *
 * Used to configure an AP interface.
 *
 * @chandef: defines the channel to use
 * @beacon: beacon data
 * @beacon_interval: beacon interval
 * @dtim_period: DTIM period
 * @ssid: SSID to be used in the BSS (note: may be %NULL if not provided from
 *  user space)
 * @ssid_len: length of @ssid
 * @hidden_ssid: whether to hide the SSID in Beacon/Probe Response frames
 * @crypto: crypto settings
 * @privacy: the BSS uses privacy
 * @auth_type: Authentication type (algorithm)
 * @smps_mode: SMPS mode
 * @inactivity_timeout: time in seconds to determine station's inactivity.
 * @p2p_ctwindow: P2P CT Window
 * @p2p_opp_ps: P2P opportunistic PS
 * @acl: ACL configuration used by the drivers which has support for
 *  MAC address based access control
 * @pbss: If set, start as a PCP instead of AP. Relevant for DMG
 *  networks.
 * @beacon_rate: bitrate to be used for beacons
 * @ht_cap: HT capabilities (or %NULL if HT isn't enabled)
 * @vht_cap: VHT capabilities (or %NULL if VHT isn't enabled)
 * @he_cap: HE capabilities (or %NULL if HE isn't enabled)
 * @eht_cap: EHT capabilities (or %NULL if EHT isn't enabled)
 * @eht_oper: EHT operation IE (or %NULL if EHT isn't enabled)
 * @ht_required: stations must support HT
 * @vht_required: stations must support VHT
 * @twt_responder: Enable Target Wait Time
 * @he_required: stations must support HE
 * @sae_h2e_required: stations must support direct H2E technique in SAE
 * @flags: flags, as defined in enum cfg80211_ap_settings_flags
 * @he_obss_pd: OBSS Packet Detection settings
 * @he_oper: HE operation IE (or %NULL if HE isn't enabled)
 * @fils_discovery: FILS discovery transmission parameters
 * @unsol_bcast_probe_resp: Unsolicited broadcast probe response parameters
 * @mbssid_config: AP settings for multiple bssid
 */

struct cfg80211_ap_settings
{
  struct cfg80211_chan_def chandef;
  struct cfg80211_beacon_data beacon;
  int beacon_interval;
  int dtim_period;
  const uint8_t *ssid;
  size_t ssid_len;
  enum nl80211_hidden_ssid hidden_ssid;
  struct cfg80211_crypto_settings crypto;
  bool privacy;
  enum nl80211_auth_type auth_type;
  enum nl80211_smps_mode smps_mode;
  int inactivity_timeout;
  uint8_t p2p_ctwindow;
  bool p2p_opp_ps;
  const struct cfg80211_acl_data *acl;
  bool pbss;
  struct cfg80211_bitrate_mask beacon_rate;
  const struct ieee80211_ht_cap *ht_cap;
  const struct ieee80211_vht_cap *vht_cap;
  const struct ieee80211_he_cap_elem *he_cap;
  const struct ieee80211_he_operation *he_oper;
  const struct ieee80211_eht_cap_elem *eht_cap;
  const struct ieee80211_eht_operation *eht_oper;
  bool ht_required;
  bool vht_required;
  bool he_required;
  bool sae_h2e_required;
  bool twt_responder;
  uint32_t flags;
  struct ieee80211_he_obss_pd he_obss_pd;
  struct cfg80211_fils_discovery fils_discovery;
  struct cfg80211_unsol_bcast_probe_resp unsol_bcast_probe_resp;
  struct cfg80211_mbssid_config mbssid_config;
};

/**
 * struct cfg80211_csa_settings - channel switch settings
 *
 * Used for channel switch announcements.
 *
 * @chandef: defines the channel to use after the switch
 * @beacon_csa: beacon data while performing the switch
 * @counter_offsets_beacon: offsets of the counters within the beacon (tail)
 * @counter_offsets_presp: offsets of the counters within the probe response
 * @n_counter_offsets_beacon: number of csa counters in the beacon (tail)
 * @n_counter_offsets_presp: number of csa counters in the probe response
 * @beacon_after: beacon data to be used on the new channel
 * @radar_required: whether radar detection is required on the new channel
 * @block_tx: whether transmissions should be blocked while changing
 * @count: number of beacons until switch
 */

struct cfg80211_csa_settings
{
  struct cfg80211_chan_def chandef;
  struct cfg80211_beacon_data beacon_csa;
  const uint16_t *counter_offsets_beacon;
  const uint16_t *counter_offsets_presp;
  unsigned int n_counter_offsets_beacon;
  unsigned int n_counter_offsets_presp;
  struct cfg80211_beacon_data beacon_after;
  bool radar_required;
  bool block_tx;
  uint8_t count;
};

/**
 * struct sta_txpwr - station txpower configuration
 *
 * Used to configure transmit power for a station.
 *
 * @power: tx power (in dBm) to be used for sending data traffic. If
 *  tx power is not provided, the default per-interface tx power setting
 *  will be overriding. Driver should be picking up the lowest tx power,
 *  either tx power per-interface or per-station.
 * @type: In particular if TPC @type is NL80211_TX_POWER_LIMITED then
 *  tx power will be less than or equal to specified from userspace,
 *  whereas if TPC @type is NL80211_TX_POWER_AUTOMATIC then it indicates
 *  default tx power. NL80211_TX_POWER_FIXED is not a valid configuration
 *  option for per peer TPC.
 */

struct sta_txpwr
{
  int16_t power;
  enum nl80211_tx_power_setting type;
};

/**
 * struct link_station_parameters - link station parameters
 *
 * Used to change and create a new link station.
 *
 * @mld_mac: MAC address of the station
 * @link_id: the link id (-1 for non-MLD station)
 * @link_mac: MAC address of the link
 * @supported_rates: supported rates in IEEE 802.11 format (or NULL
 *  for no change)
 * @supported_rates_len: number of supported rates
 * @ht_capa: HT capabilities of station
 * @vht_capa: VHT capabilities of station
 * @opmode_notif: operating mode field from Operating Mode Notification
 * @opmode_notif_used: information if operating mode field is used
 * @he_capa: HE capabilities of station
 * @he_capa_len: the length of the HE capabilities
 * @txpwr: transmit power for an associated station
 * @txpwr_set: txpwr field is set
 * @he_6ghz_capa: HE 6 GHz Band capabilities of station
 * @eht_capa: EHT capabilities of station
 * @eht_capa_len: the length of the EHT capabilities
 */

struct link_station_parameters
{
  const uint8_t *mld_mac;
  int link_id;
  const uint8_t *link_mac;
  const uint8_t *supported_rates;
  uint8_t supported_rates_len;
  const struct ieee80211_ht_cap *ht_capa;
  const struct ieee80211_vht_cap *vht_capa;
  uint8_t opmode_notif;
  bool opmode_notif_used;
  const struct ieee80211_he_cap_elem *he_capa;
  uint8_t he_capa_len;
  struct sta_txpwr txpwr;
  bool txpwr_set;
  const struct ieee80211_he_6ghz_capa *he_6ghz_capa;
  const struct ieee80211_eht_cap_elem *eht_capa;
  uint8_t eht_capa_len;
};

/**
 * struct link_station_del_parameters - link station deletion parameters
 *
 * Used to delete a link station entry (or all stations).
 *
 * @mld_mac: MAC address of the station
 * @link_id: the link id
 */

struct link_station_del_parameters
{
  const uint8_t *mld_mac;
  uint32_t link_id;
};

/**
 * struct station_parameters - station parameters
 *
 * Used to change and create a new station.
 *
 * @vlan: vlan interface station should belong to
 * @sta_flags_mask: station flags that changed (bitmask of
 *  BIT(NL80211_STA_FLAG_...))
 * @sta_flags_set: station flags values (bitmask of
 *  BIT(NL80211_STA_FLAG_...))
 * @listen_interval: listen interval or -1 for no change
 * @aid: AID or zero for no change
 * @vlan_id: VLAN ID for station (if nonzero)
 * @peer_aid: mesh peer AID or zero for no change
 * @plink_action: plink action to take
 * @plink_state: set the peer link state for a station
 * @uapsd_queues: bitmap of queues configured for uapsd. same format as
 *  the AC bitmap in the QoS info field
 * @max_sp: max Service Period. same format as the MAX_SP in the QoS info
 *  field (but already shifted down)
 * @sta_modify_mask: bitmap indicating which parameters changed (for those
 *  that don't have a natural "no change" value), see &enum
 *  station_parameters_apply_mask
 * @local_pm: local link-specific mesh power save mode (no change when set
 *  to unknown)
 * @capability: station capability
 * @ext_capab: extended capabilities of the station
 * @ext_capab_len: number of extended capabilities
 * @supported_channels: supported channels in IEEE 802.11 format
 * @supported_channels_len: number of supported channels
 * @supported_oper_classes: supported oper classes in IEEE 802.11 format
 * @supported_oper_classes_len: number of supported operating classes
 * @support_p2p_ps: information if station supports P2P PS mechanism
 * @airtime_weight: airtime scheduler weight for this station
 * @link_sta_params: link related params
 */

struct station_parameters
{
  struct net_driver_s *vlan;
  uint32_t sta_flags_mask;
  uint32_t sta_flags_set;
  uint32_t sta_modify_mask;
  int listen_interval;
  uint16_t aid;
  uint16_t vlan_id;
  uint16_t peer_aid;
  uint8_t plink_action;
  uint8_t plink_state;
  uint8_t uapsd_queues;
  uint8_t max_sp;
  uint16_t capability;
  const uint8_t *ext_capab;
  uint8_t ext_capab_len;
  const uint8_t *supported_channels;
  uint8_t supported_channels_len;
  const uint8_t *supported_oper_classes;
  uint8_t supported_oper_classes_len;
  int support_p2p_ps;
  uint16_t airtime_weight;
  struct link_station_parameters link_sta_params;
};

/**
 * struct station_del_parameters - station deletion parameters
 *
 * Used to delete a station entry (or all stations).
 *
 * @mac: MAC address of the station to remove or NULL to remove all stations
 * @subtype: Management frame subtype to use for indicating removal
 *  (10 = Disassociation, 12 = Deauthentication)
 * @reason_code: Reason code for the Disassociation/Deauthentication frame
 */

struct station_del_parameters
{
  const uint8_t *mac;
  uint8_t subtype;
  uint16_t reason_code;
};

/**
 * enum cfg80211_station_type - the type of station being modified
 * @CFG80211_STA_AP_CLIENT: client of an AP interface
 * @CFG80211_STA_AP_CLIENT_UNASSOC: client of an AP interface that is
 *  still unassociated (update properties for this type of client is
 *  permitted)
 * @CFG80211_STA_AP_MLME_CLIENT: client of an AP interface that has the AP
 *  MLME in the device
 * @CFG80211_STA_AP_STA: AP station on managed interface
 * @CFG80211_STA_IBSS: IBSS station
 * @CFG80211_STA_TDLS_PEER_SETUP: TDLS peer on managed interface (dummy
 *  entry while TDLS setup is in progress, it moves out of this state when
 *  being marked authorized; use this only if TDLS with external setup is
 *  supported/used)
 * @CFG80211_STA_TDLS_PEER_ACTIVE: TDLS peer on managed interface (active
 *  entry that is operating, has been marked authorized by userspace)
 * @CFG80211_STA_MESH_PEER_KERNEL: peer on mesh interface (kernel managed)
 * @CFG80211_STA_MESH_PEER_USER: peer on mesh interface (user managed)
 */

enum cfg80211_station_type
{
  CFG80211_STA_AP_CLIENT,
  CFG80211_STA_AP_CLIENT_UNASSOC,
  CFG80211_STA_AP_MLME_CLIENT,
  CFG80211_STA_AP_STA,
  CFG80211_STA_IBSS,
  CFG80211_STA_TDLS_PEER_SETUP,
  CFG80211_STA_TDLS_PEER_ACTIVE,
  CFG80211_STA_MESH_PEER_KERNEL,
  CFG80211_STA_MESH_PEER_USER
};

/**
 * enum rate_info_flags - bitrate info flags
 *
 * Used by the driver to indicate the specific rate transmission
 * type for 802.11n transmissions.
 *
 * @RATE_INFO_FLAGS_MCS: mcs field filled with HT MCS
 * @RATE_INFO_FLAGS_VHT_MCS: mcs field filled with VHT MCS
 * @RATE_INFO_FLAGS_SHORT_GI: 400ns guard interval
 * @RATE_INFO_FLAGS_DMG: 60GHz MCS
 * @RATE_INFO_FLAGS_HE_MCS: HE MCS information
 * @RATE_INFO_FLAGS_EDMG: 60GHz MCS in EDMG mode
 * @RATE_INFO_FLAGS_EXTENDED_SC_DMG: 60GHz extended SC MCS
 * @RATE_INFO_FLAGS_EHT_MCS: EHT MCS information
 */

enum rate_info_flags
{
  RATE_INFO_FLAGS_MCS             = 1 << 0,
  RATE_INFO_FLAGS_VHT_MCS         = 1 << 1,
  RATE_INFO_FLAGS_SHORT_GI        = 1 << 2,
  RATE_INFO_FLAGS_DMG             = 1 << 3,
  RATE_INFO_FLAGS_HE_MCS          = 1 << 4,
  RATE_INFO_FLAGS_EDMG            = 1 << 5,
  RATE_INFO_FLAGS_EXTENDED_SC_DMG = 1 << 6,
  RATE_INFO_FLAGS_EHT_MCS         = 1 << 7
};

/**
 * enum rate_info_bw - bandwidth values for rate information
 *
 * Used to indicate the bandwidth for rate information.
 *
 * @RATE_INFO_BW_20: 20 MHz bandwidth
 * @RATE_INFO_BW_5: 5 MHz bandwidth
 * @RATE_INFO_BW_10: 10 MHz bandwidth
 * @RATE_INFO_BW_40: 40 MHz bandwidth
 * @RATE_INFO_BW_80: 80 MHz bandwidth
 * @RATE_INFO_BW_160: 160 MHz bandwidth
 * @RATE_INFO_BW_HE_RU: Bandwidth determined by HE RU allocation
 * @RATE_INFO_BW_320: 320 MHz bandwidth
 * @RATE_INFO_BW_EHT_RU: Bandwidth determined by EHT RU allocation
 */

enum rate_info_bw
{
  RATE_INFO_BW_20     = 0,
  RATE_INFO_BW_5      = 1,
  RATE_INFO_BW_10     = 2,
  RATE_INFO_BW_40     = 3,
  RATE_INFO_BW_80     = 4,
  RATE_INFO_BW_160    = 5,
  RATE_INFO_BW_HE_RU  = 6,
  RATE_INFO_BW_320    = 7,
  RATE_INFO_BW_EHT_RU = 8
};

/**
 * struct rate_info - bitrate information
 *
 * Information about a receiving or transmitting bitrate
 *
 * @flags: bitflag of flags from &enum rate_info_flags
 * @mcs: mcs index if struct describes an HT/VHT/HE rate
 * @legacy: bitrate in 100kbit/s for 802.11abg
 * @nss: number of streams (VHT & HE only)
 * @bw: bandwidth (from &enum rate_info_bw)
 * @he_gi: HE guard interval (from &enum nl80211_he_gi)
 * @he_dcm: HE DCM value
 * @he_ru_alloc: HE RU allocation (from &enum nl80211_he_ru_alloc,
 *  only valid if bw is %RATE_INFO_BW_HE_RU)
 * @n_bonded_ch: In case of EDMG the number of bonded channels (1-4)
 * @eht_gi: EHT guard interval (from &enum nl80211_eht_gi)
 * @eht_ru_alloc: EHT RU allocation (from &enum nl80211_eht_ru_alloc,
 *  only valid if bw is %RATE_INFO_BW_EHT_RU)
 */

struct rate_info
{
  uint8_t flags;
  uint8_t mcs;
  uint16_t legacy;
  uint8_t nss;
  uint8_t bw;
  uint8_t he_gi;
  uint8_t he_dcm;
  uint8_t he_ru_alloc;
  uint8_t n_bonded_ch;
  uint8_t eht_gi;
  uint8_t eht_ru_alloc;
};

/**
 * enum bss_param_flags - bitrate info flags
 *
 * Used by the driver to indicate the specific rate transmission
 * type for 802.11n transmissions.
 *
 * @BSS_PARAM_FLAGS_CTS_PROT: whether CTS protection is enabled
 * @BSS_PARAM_FLAGS_SHORT_PREAMBLE: whether short preamble is enabled
 * @BSS_PARAM_FLAGS_SHORT_SLOT_TIME: whether short slot time is enabled
 */

enum bss_param_flags
{
  BSS_PARAM_FLAGS_CTS_PROT        = 1 << 0,
  BSS_PARAM_FLAGS_SHORT_PREAMBLE  = 1 << 1,
  BSS_PARAM_FLAGS_SHORT_SLOT_TIME = 1 << 2
};

/**
 * struct sta_bss_parameters - BSS parameters for the attached station
 *
 * Information about the currently associated BSS
 *
 * @flags: bitflag of flags from &enum bss_param_flags
 * @dtim_period: DTIM period for the BSS
 * @beacon_interval: beacon interval
 */

struct sta_bss_parameters
{
  uint8_t flags;
  uint8_t dtim_period;
  uint16_t beacon_interval;
};

/**
 * struct cfg80211_txq_stats - TXQ statistics for this TID
 * @filled: bitmap of flags using the bits of &enum nl80211_txq_stats to
 *  indicate the relevant values in this struct are filled
 * @backlog_bytes: total number of bytes currently backlogged
 * @backlog_packets: total number of packets currently backlogged
 * @flows: number of new flows seen
 * @drops: total number of packets dropped
 * @ecn_marks: total number of packets marked with ECN CE
 * @overlimit: number of drops due to queue space overflow
 * @overmemory: number of drops due to memory limit overflow
 * @collisions: number of hash collisions
 * @tx_bytes: total number of bytes dequeued
 * @tx_packets: total number of packets dequeued
 * @max_flows: maximum number of flows supported
 */

struct cfg80211_txq_stats
{
  uint32_t filled;
  uint32_t backlog_bytes;
  uint32_t backlog_packets;
  uint32_t flows;
  uint32_t drops;
  uint32_t ecn_marks;
  uint32_t overlimit;
  uint32_t overmemory;
  uint32_t collisions;
  uint32_t tx_bytes;
  uint32_t tx_packets;
  uint32_t max_flows;
};

/**
 * struct cfg80211_tid_stats - per-TID statistics
 * @filled: bitmap of flags using the bits of &enum nl80211_tid_stats to
 *  indicate the relevant values in this struct are filled
 * @rx_msdu: number of received MSDUs
 * @tx_msdu: number of (attempted) transmitted MSDUs
 * @tx_msdu_retries: number of retries (not counting the first) for
 *  transmitted MSDUs
 * @tx_msdu_failed: number of failed transmitted MSDUs
 * @txq_stats: TXQ statistics
 */

struct cfg80211_tid_stats
{
  uint32_t filled;
  uint64_t rx_msdu;
  uint64_t tx_msdu;
  uint64_t tx_msdu_retries;
  uint64_t tx_msdu_failed;
  struct cfg80211_txq_stats txq_stats;
};

#define IEEE80211_MAX_CHAINS	4

/**
 * struct station_info - station information
 *
 * Station information filled by driver for get_station() and dump_station.
 *
 * @filled: bitflag of flags using the bits of &enum nl80211_sta_info to
 *  indicate the relevant values in this struct for them
 * @connected_time: time(in secs) since a station is last connected
 * @inactive_time: time since last station activity (tx/rx) in milliseconds
 * @assoc_at: bootime (ns) of the last association
 * @rx_bytes: bytes (size of MPDUs) received from this station
 * @tx_bytes: bytes (size of MPDUs) transmitted to this station
 * @llid: mesh local link id
 * @plid: mesh peer link id
 * @plink_state: mesh peer link state
 * @signal: The signal strength, type depends on the wiphy's signal_type.
 *  For CFG80211_SIGNAL_TYPE_MBM, value is expressed in _dBm_.
 * @signal_avg: Average signal strength, type depends on the wiphy's
 *  signal_type. For CFG80211_SIGNAL_TYPE_MBM, value is expressed in _dBm_.
 * @chains: bitmask for filled values in @chain_signal, @chain_signal_avg
 * @chain_signal: per-chain signal strength of last received packet in dBm
 * @chain_signal_avg: per-chain signal strength average in dBm
 * @txrate: current unicast bitrate from this station
 * @rxrate: current unicast bitrate to this station
 * @rx_packets: packets (MSDUs & MMPDUs) received from this station
 * @tx_packets: packets (MSDUs & MMPDUs) transmitted to this station
 * @tx_retries: cumulative retry counts (MPDUs)
 * @tx_failed: number of failed transmissions (MPDUs) (retries exceeded,
 *  no ACK)
 * @rx_dropped_misc:  Dropped for un-specified reason.
 * @bss_param: current BSS parameters
 * @generation: generation number for nl80211 dumps.
 *  This number should increase every time the list of stations
 *  changes, i.e. when a station is added or removed, so that
 *  userspace can tell whether it got a consistent snapshot.
 * @assoc_req_ies: IEs from (Re)Association Request.
 *  This is used only when in AP mode with drivers that do not use
 *  user space MLME/SME implementation. The information is provided for
 *  the cfg80211_new_sta() calls to notify user space of the IEs.
 * @assoc_req_ies_len: Length of assoc_req_ies buffer in octets.
 * @sta_flags: station flags mask & values
 * @beacon_loss_count: Number of times beacon loss event has triggered.
 * @t_offset: Time offset of the station relative to this host.
 * @local_pm: local mesh STA power save mode
 * @peer_pm: peer mesh STA power save mode
 * @nonpeer_pm: non-peer mesh STA power save mode
 * @expected_throughput: expected throughput in kbps (including
 *  802.11 MAC headers) towards this station.
 * @rx_beacon: number of beacons received from this peer
 * @rx_beacon_signal_avg: signal strength average (in dBm) for beacons
 *  received from this peer
 * @connected_to_gate: true if mesh STA has a path to mesh gate
 * @rx_duration: aggregate PPDU duration(usecs) for all the frames from this
 *  peer
 * @tx_duration: aggregate PPDU duration(usecs) for all the frames to a peer
 * @airtime_weight: current airtime scheduling weight
 * @pertid: per-TID statistics, see &struct cfg80211_tid_stats, using the
 *  last (IEEE80211_NUM_TIDS) index for MSDUs not encapsulated in QoS-MPDUs.
 *  Note that this doesn't use the @filled bit, but is used if non-NULL.
 * @ack_signal: signal strength (in dBm) of the last ACK frame.
 * @avg_ack_signal: average rssi value of ack packet for the no of msdu's has
 *  been sent.
 * @rx_mpdu_count: number of MPDUs received from this station
 * @fcs_err_count: number of packets (MPDUs) received from this station with
 *  an FCS error. This counter should be incremented only when TA of the
 *  received packet with an FCS error matches the peer MAC address.
 * @airtime_link_metric: mesh airtime link metric.
 * @connected_to_as: true if mesh STA has a path to authentication server
 */

struct station_info
{
  uint64_t filled;
  uint32_t connected_time;
  uint32_t inactive_time;
  uint64_t assoc_at;
  uint64_t rx_bytes;
  uint64_t tx_bytes;
  uint16_t llid;
  uint16_t plid;
  uint8_t plink_state;
  int8_t signal;
  int8_t signal_avg;

  uint8_t chains;
  int8_t chain_signal[IEEE80211_MAX_CHAINS];
  int8_t chain_signal_avg[IEEE80211_MAX_CHAINS];

  struct rate_info txrate;
  struct rate_info rxrate;
  uint32_t rx_packets;
  uint32_t tx_packets;
  uint32_t tx_retries;
  uint32_t tx_failed;
  uint32_t rx_dropped_misc;
  struct sta_bss_parameters bss_param;
  struct nl80211_sta_flag_update sta_flags;

  int generation;

  const uint8_t *assoc_req_ies;
  size_t assoc_req_ies_len;

  uint32_t beacon_loss_count;
  int64_t t_offset;

  uint32_t expected_throughput;

  uint64_t tx_duration;
  uint64_t rx_duration;
  uint64_t rx_beacon;
  uint8_t rx_beacon_signal_avg;
  uint8_t connected_to_gate;

  struct cfg80211_tid_stats *pertid;
  int8_t ack_signal;
  int8_t avg_ack_signal;
  uint16_t airtime_weight;
  uint32_t rx_mpdu_count;
  uint32_t fcs_err_count;
  uint32_t airtime_link_metric;
  uint8_t connected_to_as;
};

/**
 * struct cfg80211_sar_freq_ranges - sar frequency ranges
 * @start_freq:  start range edge frequency
 * @end_freq:    end range edge frequency
 */

struct cfg80211_sar_freq_ranges
{
  uint32_t start_freq;
  uint32_t end_freq;
};

/**
 * struct cfg80211_sar_capa - sar limit capability
 * @type: it's set via power in 0.25dbm or other types
 * @num_freq_ranges: number of frequency ranges
 * @freq_ranges: memory to hold the freq ranges.
 *
 * Note: WLAN driver may append new ranges or split an existing
 * range to small ones and then append them.
 */

struct cfg80211_sar_capa
{
  enum nl80211_sar_type type;
  uint32_t num_freq_ranges;
  const struct cfg80211_sar_freq_ranges *freq_ranges;
};

/**
 * struct ieee80211_txq_params - TX queue parameters
 * @ac: AC identifier
 * @txop: Maximum burst time in units of 32 usecs, 0 meaning disabled
 * @cwmin: Minimum contention window [a value of the form 2^n-1 in the range
 *  1..32767]
 * @cwmax: Maximum contention window [a value of the form 2^n-1 in the range
 *  1..32767]
 * @aifs: Arbitration interframe space [0..255]
 * @link_id: link_id or -1 for non-MLD
 */

struct ieee80211_txq_params
{
  enum nl80211_ac ac;
  uint16_t txop;
  uint16_t cwmin;
  uint16_t cwmax;
  uint8_t aifs;
  int link_id;
};

/**
 * DOC: Scanning and BSS list handling
 *
 * The scanning process itself is fairly simple, but cfg80211 offers quite
 * a bit of helper functionality. To start a scan, the scan operation will
 * be invoked with a scan definition. This scan definition contains the
 * channels to scan, and the SSIDs to send probe requests for (including the
 * wildcard, if desired). A passive scan is indicated by having no SSIDs to
 * probe. Additionally, a scan request may contain extra information elements
 * that should be added to the probe request. The IEs are guaranteed to be
 * well-formed, and will not exceed the maximum length the driver advertised
 * in the wiphy structure.
 *
 * When scanning finds a BSS, cfg80211 needs to be notified of that, because
 * it is responsible for maintaining the BSS list; the driver should not
 * maintain a list itself. For this notification, various functions exist.
 *
 * Since drivers do not maintain a BSS list, there are also a number of
 * functions to search for a BSS and obtain information about it from the
 * BSS structure cfg80211 maintains. The BSS list is also made available
 * to userspace.
 */

/**
 * struct cfg80211_ssid - SSID description
 * @ssid: the SSID
 * @ssid_len: length of the ssid
 */

struct cfg80211_ssid
{
  uint8_t ssid[IEEE80211_MAX_SSID_LEN];
  uint8_t ssid_len;
};

/**
 * struct cfg80211_scan_info - information about completed scan
 * @scan_start_tsf: scan start time in terms of the TSF of the BSS that the
 *  wireless device that requested the scan is connected to. If this
 *  information is not available, this field is left zero.
 * @tsf_bssid: the BSSID according to which %scan_start_tsf is set.
 * @aborted: set to true if the scan was aborted for any reason,
 *  userspace will be notified of that
 */

struct cfg80211_scan_info
{
  uint64_t scan_start_tsf;
  uint8_t tsf_bssid[ETH_ALEN] __attribute__((aligned(2)));
  bool aborted;
};

/**
 * struct cfg80211_scan_6ghz_params - relevant for 6 GHz only
 *
 * @short_ssid: short ssid to scan for
 * @bssid: bssid to scan for
 * @channel_idx: idx of the channel in the channel array in the scan
 *  request which the above info relvant to
 * @unsolicited_probe: the AP transmits unsolicited probe response every
 *  20 TU
 * @short_ssid_valid: @short_ssid is valid and can be used
 * @psc_no_listen: when set, and the channel is a PSC channel, no need to
 *  wait 20 TUs before starting to send probe requests.
 */

struct cfg80211_scan_6ghz_params
{
  uint32_t short_ssid;
  uint32_t channel_idx;
  uint8_t bssid[ETH_ALEN];
  bool unsolicited_probe;
  bool short_ssid_valid;
  bool psc_no_listen;
};

/**
 * struct cfg80211_scan_request - scan request description
 *
 * @ssids: SSIDs to scan for (active scan only)
 * @n_ssids: number of SSIDs
 * @channels: channels to scan on.
 * @n_channels: total number of channels to scan
 * @scan_width: channel width for scanning
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @duration: how long to listen on each channel, in TUs. If
 *  %duration_mandatory is not set, this is the maximum dwell time and
 *  the actual dwell time may be shorter.
 * @duration_mandatory: if set, the scan duration must be as specified by the
 *  %duration field.
 * @flags: bit field of flags controlling operation
 * @rates: bitmap of rates to advertise for each band
 * @wiphy: the wiphy this was for
 * @scan_start: time (in jiffies) when the scan started
 * @wdev: the wireless device to scan for
 * @info: (internal) information about completed scan
 * @notified: (internal) scan request was notified as done or aborted
 * @no_cck: used to send probe requests at non CCK rate in 2GHz band
 * @mac_addr: MAC address used with randomisation
 * @mac_addr_mask: MAC address mask used with randomisation, bits that
 *  are 0 in the mask should be randomised, bits that are 1 should
 *  be taken from the @mac_addr
 * @scan_6ghz: relevant for split scan request only,
 *  true if this is the second scan request
 * @n_6ghz_params: number of 6 GHz params
 * @scan_6ghz_params: 6 GHz params
 * @bssid: BSSID to scan for (most commonly, the wildcard BSSID)
 */

struct cfg80211_scan_request
{
  struct cfg80211_ssid *ssids;
  int n_ssids;
  uint32_t n_channels;
  enum nl80211_bss_scan_width scan_width;
  const uint8_t *ie;
  size_t ie_len;
  uint16_t duration;
  bool duration_mandatory;
  uint32_t flags;

  uint32_t rates[NUM_NL80211_BANDS];

  struct wireless_dev *wdev;

  uint8_t mac_addr[ETH_ALEN] __attribute__((aligned(2)));
  uint8_t mac_addr_mask[ETH_ALEN] __attribute__((aligned(2)));
  uint8_t bssid[ETH_ALEN] __attribute__((aligned(2)));

  struct wiphy *wiphy;
  unsigned long scan_start;
  struct cfg80211_scan_info info;
  bool notified;
  bool no_cck;
  bool scan_6ghz;
  uint32_t n_6ghz_params;
  struct cfg80211_scan_6ghz_params *scan_6ghz_params;

  struct ieee80211_channel *channels[];
};

/**
 * struct cfg80211_match_set - sets of attributes to match
 *
 * @ssid: SSID to be matched; may be zero-length in case of BSSID match
 *  or no match (RSSI only)
 * @bssid: BSSID to be matched; may be all-zero BSSID in case of SSID match
 *  or no match (RSSI only)
 * @rssi_thold: don't report scan results below this threshold (in
 *  int32_t dBm)
 * @per_band_rssi_thold: Minimum rssi threshold for each band to be applied
 *  for filtering out scan results received. Drivers advertize this support
 *  of band specific rssi based filtering through the feature capability
 *  %NL80211_EXT_FEATURE_SCHED_SCAN_BAND_SPECIFIC_RSSI_THOLD. These band
 *  specific rssi thresholds take precedence over rssi_thold, if specified.
 *  If not specified for any band, it will be assigned with rssi_thold of
 *  corresponding matchset.
 */

struct cfg80211_match_set
{
  struct cfg80211_ssid ssid;
  uint8_t bssid[ETH_ALEN];
  int32_t rssi_thold;
  int32_t per_band_rssi_thold[NUM_NL80211_BANDS];
};

/**
 * struct cfg80211_sched_scan_plan - scan plan for scheduled scan
 *
 * @interval: interval between scheduled scan iterations. In seconds.
 * @iterations: number of scan iterations in this scan plan. Zero means
 *  infinite loop.
 *  The last scan plan will always have this parameter set to zero,
 *  all other scan plans will have a finite number of iterations.
 */

struct cfg80211_sched_scan_plan
{
  uint32_t interval;
  uint32_t iterations;
};

/**
 * struct cfg80211_bss_select_adjust - BSS selection with RSSI adjustment.
 *
 * @band: band of BSS which should match for RSSI level adjustment.
 * @delta: value of RSSI level adjustment.
 */

struct cfg80211_bss_select_adjust
{
  enum nl80211_band band;
  int8_t delta;
};

/**
 * struct cfg80211_sched_scan_request - scheduled scan request description
 *
 * @reqid: identifies this request.
 * @ssids: SSIDs to scan for (passed in the probe_reqs in active scans)
 * @n_ssids: number of SSIDs
 * @n_channels: total number of channels to scan
 * @scan_width: channel width for scanning
 * @ie: optional information element(s) to add into Probe Request or %NULL
 * @ie_len: length of ie in octets
 * @flags: bit field of flags controlling operation
 * @match_sets: sets of parameters to be matched for a scan result
 *  entry to be considered valid and to be passed to the host
 *  (others are filtered out).
 *  If ommited, all results are passed.
 * @n_match_sets: number of match sets
 * @report_results: indicates that results were reported for this request
 * @wiphy: the wiphy this was for
 * @dev: the interface
 * @scan_start: start time of the scheduled scan
 * @channels: channels to scan
 * @min_rssi_thold: for drivers only supporting a single threshold, this
 *  contains the minimum over all matchsets
 * @mac_addr: MAC address used with randomisation
 * @mac_addr_mask: MAC address mask used with randomisation, bits that
 *  are 0 in the mask should be randomised, bits that are 1 should
 *  be taken from the @mac_addr
 * @scan_plans: scan plans to be executed in this scheduled scan. Lowest
 *  index must be executed first.
 * @n_scan_plans: number of scan plans, at least 1.
 * @rcu_head: RCU callback used to free the struct
 * @owner_nlportid: netlink portid of owner (if this should is a request
 *  owned by a particular socket)
 * @nl_owner_dead: netlink owner socket was closed - this request be freed
 * @list: for keeping list of requests.
 * @delay: delay in seconds to use before starting the first scan
 *  cycle.  The driver may ignore this parameter and start
 *  immediately (or at any other time), if this feature is not
 *  supported.
 * @relative_rssi_set: Indicates whether @relative_rssi is set or not.
 * @relative_rssi: Relative RSSI threshold in dB to restrict scan result
 *  reporting in connected state to cases where a matching BSS is determined
 *  to have better or slightly worse RSSI than the current connected BSS.
 *  The relative RSSI threshold values are ignored in disconnected state.
 * @rssi_adjust: delta dB of RSSI preference to be given to the BSSs that
 *  belong to the specified band while deciding whether a better BSS is
 *  reported using @relative_rssi. If delta is a negative number, the BSSs
 *  that belong to the specified band will be penalized by delta dB in
 *  relative comparisions.
 */

struct cfg80211_sched_scan_request
{
  uint64_t reqid;
  struct cfg80211_ssid *ssids;
  int n_ssids;
  uint32_t n_channels;
  enum nl80211_bss_scan_width scan_width;
  const uint8_t *ie;
  size_t ie_len;
  uint32_t flags;
  struct cfg80211_match_set *match_sets;
  int n_match_sets;
  int32_t min_rssi_thold;
  uint32_t delay;
  struct cfg80211_sched_scan_plan *scan_plans;
  int n_scan_plans;

  uint8_t mac_addr[ETH_ALEN] __attribute__((aligned(2)));
  uint8_t mac_addr_mask[ETH_ALEN] __attribute__((aligned(2)));

  bool relative_rssi_set;
  int8_t relative_rssi;
  struct cfg80211_bss_select_adjust rssi_adjust;

  /* internal */

  struct wiphy *wiphy;
  struct net_driver_s *dev;
  unsigned long scan_start;
  bool report_results;
  uint32_t owner_nlportid;
  bool nl_owner_dead;
  struct list_node list;
  struct ieee80211_channel *channels[];
};

/**
 * enum cfg80211_signal_type - signal type
 *
 * @CFG80211_SIGNAL_TYPE_NONE: no signal strength information available
 * @CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm)
 * @CFG80211_SIGNAL_TYPE_UNSPEC: signal strength, increasing from 0
 *  through 100
 */

enum cfg80211_signal_type
{
  CFG80211_SIGNAL_TYPE_NONE,
  CFG80211_SIGNAL_TYPE_MBM,
  CFG80211_SIGNAL_TYPE_UNSPEC,
};

/**
 * struct cfg80211_bss_ies - BSS entry IE data
 * @tsf: TSF contained in the frame that carried these IEs
 * @rcu_head: internal use, for freeing
 * @len: length of the IEs
 * @from_beacon: these IEs are known to come from a beacon
 * @data: IE data
 */

struct cfg80211_bss_ies
{
  uint64_t tsf;
  int len;
  bool from_beacon;
  uint8_t data[];
};

/**
 * struct cfg80211_bss - BSS description
 *
 * This structure describes a BSS (which may also be a mesh network)
 * for use in scan results and similar.
 *
 * @channel: channel this BSS is on
 * @scan_width: width of the control channel
 * @bssid: BSSID of the BSS
 * @beacon_interval: the beacon interval as from the frame
 * @capability: the capability field in host byte order
 * @ies: the information elements (Note that there is no guarantee that these
 *  are well-formed!); this is a pointer to either the beacon_ies or
 *  proberesp_ies depending on whether Probe Response frame has been
 *  received. It is always non-%NULL.
 * @beacon_ies: the information elements from the last Beacon frame
 *  (implementation note: if @hidden_beacon_bss is set this struct doesn't
 *  own the beacon_ies, but they're just pointers to the ones from the
 *  @hidden_beacon_bss struct)
 * @proberesp_ies: the information elements from the last Probe Response
 *  frame
 * @hidden_beacon_bss: in case this BSS struct represents a probe response
 *  from a BSS that hides the SSID in its beacon, this points to the BSS
 *  struct that holds the beacon data. @beacon_ies is still valid, of
 *  course, and points to the same data as hidden_beacon_bss->beacon_ies in
 *  that case.
 * @transmitted_bss: pointer to the transmitted BSS, if this is a
 *  non-transmitted one (multi-BSSID support)
 * @nontrans_list: list of non-transmitted BSS, if this is a transmitted one
 *  (multi-BSSID support)
 * @signal: signal strength value (type depends on the wiphy's signal_type)
 * @chains: bitmask for filled values in @chain_signal.
 * @chain_signal: per-chain signal strength of last received BSS in dBm.
 * @bssid_index: index in the multiple BSS set
 * @max_bssid_indicator: max number of members in the BSS set
 * @priv: private area for driver use, has at least wiphy->bss_priv_size
 *  bytes
 */

struct cfg80211_bss
{
  struct ieee80211_channel *channel;
  enum nl80211_bss_scan_width scan_width;
  const struct cfg80211_bss_ies *ies;
  const struct cfg80211_bss_ies *beacon_ies;
  const struct cfg80211_bss_ies *proberesp_ies;
  struct cfg80211_bss *hidden_beacon_bss;
  struct cfg80211_bss *transmitted_bss;
  struct list_node nontrans_list;
  int32_t signal;
  uint16_t beacon_interval;
  uint16_t capability;
  uint8_t bssid[ETH_ALEN];
  uint8_t chains;
  int8_t chain_signal[IEEE80211_MAX_CHAINS];
  uint8_t bssid_index;
  uint8_t max_bssid_indicator;
  uint8_t priv[] __attribute__((aligned(sizeof(void *))));
};

/**
 * struct cfg80211_auth_request - Authentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication.
 *
 * @bss: The BSS to authenticate with, the callee must obtain a reference
 *  to it if it needs to keep it.
 * @auth_type: Authentication type (algorithm)
 * @ie: Extra IEs to add to Authentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @key_len: length of WEP key for shared key authentication
 * @key_idx: index of WEP key for shared key authentication
 * @key: WEP key for shared key authentication
 * @auth_data: Fields and elements in Authentication frames. This contains
 *  the authentication frame body (non-IE and IE data), excluding the
 *  Authentication algorithm number, i.e., starting at the Authentication
 *  transaction sequence number field.
 * @auth_data_len: Length of auth_data buffer in octets
 * @link_id: if >= 0, indicates authentication should be done as an MLD,
 *  the interface address is included as the MLD address and the
 *  necessary link (with the given link_id) will be created (and
 *  given an MLD address) by the driver
 * @ap_mld_addr: AP MLD address in case of authentication request with
 *  an AP MLD, valid iff @link_id >= 0
 */

struct cfg80211_auth_request
{
  struct cfg80211_bss *bss;
  const uint8_t *ie;
  size_t ie_len;
  enum nl80211_auth_type auth_type;
  const uint8_t *key;
  uint8_t key_len;
  int8_t key_idx;
  const uint8_t *auth_data;
  size_t auth_data_len;
  int8_t link_id;
  const uint8_t *ap_mld_addr;
};

/**
 * struct cfg80211_assoc_link - per-link information for MLO association
 * @bss: the BSS pointer, see also &struct cfg80211_assoc_request::bss;
 *  if this is %NULL for a link, that link is not requested
 * @elems: extra elements for the per-STA profile for this link
 * @elems_len: length of the elements
 */

struct cfg80211_assoc_link
{
  struct cfg80211_bss *bss;
  const uint8_t *elems;
  size_t elems_len;
};

/**
 * enum cfg80211_assoc_req_flags - Over-ride default behaviour
 *  in association.
 *
 * @ASSOC_REQ_DISABLE_HT:  Disable HT (802.11n)
 * @ASSOC_REQ_DISABLE_VHT:  Disable VHT
 * @ASSOC_REQ_USE_RRM: Declare RRM capability in this association
 * @CONNECT_REQ_EXTERNAL_AUTH_SUPPORT: User space indicates external
 *  authentication capability. Drivers can offload authentication to
 *  userspace if this flag is set. Only applicable for cfg80211_connect()
 *  request (connect callback).
 * @ASSOC_REQ_DISABLE_HE:  Disable HE
 * @ASSOC_REQ_DISABLE_EHT:  Disable EHT
 * @CONNECT_REQ_MLO_SUPPORT: Userspace indicates support for handling
 *  MLD links. Drivers shall disable MLO features for the current association
 *  if this flag is not set.
 */

enum cfg80211_assoc_req_flags
{
  ASSOC_REQ_DISABLE_HT              = 1 << 0,
  ASSOC_REQ_DISABLE_VHT             = 1 << 1,
  ASSOC_REQ_USE_RRM                 = 1 << 2,
  CONNECT_REQ_EXTERNAL_AUTH_SUPPORT = 1 << 3,
  ASSOC_REQ_DISABLE_HE              = 1 << 4,
  ASSOC_REQ_DISABLE_EHT             = 1 << 5,
  CONNECT_REQ_MLO_SUPPORT           = 1 << 6,
};

/**
 * struct cfg80211_assoc_request - (Re)Association request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * (re)association.
 * @bss: The BSS to associate with. If the call is successful the driver is
 *  given a reference that it must give back to cfg80211_send_rx_assoc()
 *  or to cfg80211_assoc_timeout(). To ensure proper refcounting, new
 *  association requests while already associating must be rejected.
 *  This also applies to the @links.bss parameter, which is used instead
 *  of this one (it is %NULL) for MLO associations.
 * @ie: Extra IEs to add to (Re)Association Request frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @use_mfp: Use management frame protection (IEEE 802.11w) in this
 *  association
 * @crypto: crypto settings
 * @prev_bssid: previous BSSID, if not %NULL use reassociate frame. This
 *  is used to indicate a request to reassociate within the ESS instead of a
 *  request do the initial association with the ESS. When included, this is
 *  set to the BSSID of the current association, i.e., to the value that is
 *  included in the Current AP address field of the Reassociation Request
 *  frame.
 * @flags:  See &enum cfg80211_assoc_req_flags
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *  will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 * @vht_capa: VHT capability override
 * @vht_capa_mask: VHT capability mask indicating which fields to use
 * @fils_kek: FILS KEK for protecting (Re)Association Request/Response
 *  frame or %NULL if FILS is not used.
 * @fils_kek_len: Length of fils_kek in octets
 * @fils_nonces: FILS nonces (part of AAD) for protecting (Re)Association
 *  Request/Response frame or %NULL if FILS is not used. This field starts
 *  with 16 octets of STA Nonce followed by 16 octets of AP Nonce.
 * @s1g_capa: S1G capability override
 * @s1g_capa_mask: S1G capability override mask
 * @links: per-link information for MLO connections
 * @link_id: >= 0 for MLO connections, where links are given, and indicates
 *  the link on which the association request should be sent
 * @ap_mld_addr: AP MLD address in case of MLO association request,
 *  valid iff @link_id >= 0
 */

struct cfg80211_assoc_request
{
  struct cfg80211_bss *bss;
  const uint8_t *ie;
  const uint8_t *prev_bssid;
  size_t ie_len;
  struct cfg80211_crypto_settings crypto;
  bool use_mfp;
  uint32_t flags;
  struct ieee80211_ht_cap ht_capa;
  struct ieee80211_ht_cap ht_capa_mask;
  struct ieee80211_vht_cap vht_capa;
  struct ieee80211_vht_cap vht_capa_mask;
  const uint8_t *fils_kek;
  size_t fils_kek_len;
  const uint8_t *fils_nonces;
  struct ieee80211_s1g_cap s1g_capa;
  struct ieee80211_s1g_cap s1g_capa_mask;
  struct cfg80211_assoc_link links[IEEE80211_MLD_MAX_NUM_LINKS];
  const uint8_t *ap_mld_addr;
  int8_t link_id;
};

/**
 * struct cfg80211_deauth_request - Deauthentication request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * deauthentication.
 *
 * @bssid: the BSSID or AP MLD address to deauthenticate from
 * @ie: Extra IEs to add to Deauthentication frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @reason_code: The reason code for the deauthentication
 * @local_state_change: if set, change local state only and
 *  do not set a deauth frame
 */

struct cfg80211_deauth_request
{
  const uint8_t *bssid;
  const uint8_t *ie;
  size_t ie_len;
  uint16_t reason_code;
  bool local_state_change;
};

/**
 * struct cfg80211_disassoc_request - Disassociation request data
 *
 * This structure provides information needed to complete IEEE 802.11
 * disassociation.
 *
 * @ap_addr: the BSSID or AP MLD address to disassociate from
 * @ie: Extra IEs to add to Disassociation frame or %NULL
 * @ie_len: Length of ie buffer in octets
 * @reason_code: The reason code for the disassociation
 * @local_state_change: This is a request for a local state only, i.e., no
 *  Disassociation frame is to be transmitted.
 */

struct cfg80211_disassoc_request
{
  const uint8_t *ap_addr;
  const uint8_t *ie;
  size_t ie_len;
  uint16_t reason_code;
  bool local_state_change;
};

/**
 * struct cfg80211_ibss_params - IBSS parameters
 *
 * This structure defines the IBSS parameters for the join_ibss()
 * method.
 *
 * @ssid: The SSID, will always be non-null.
 * @ssid_len: The length of the SSID, will always be non-zero.
 * @bssid: Fixed BSSID requested, maybe be %NULL, if set do not
 *  search for IBSSs with a different BSSID.
 * @chandef: defines the channel to use if no other IBSS to join can be found
 * @channel_fixed: The channel should be fixed -- do not search for
 *  IBSSs to join on other channels.
 * @ie: information element(s) to include in the beacon
 * @ie_len: length of that
 * @beacon_interval: beacon interval to use
 * @privacy: this is a protected network, keys will be configured
 *  after joining
 * @control_port: whether user space controls IEEE 802.1X port, i.e.,
 *  sets/clears %NL80211_STA_FLAG_AUTHORIZED. If true, the driver is
 *  required to assume that the port is unauthorized until authorized by
 *  user space. Otherwise, port is marked authorized by default.
 * @control_port_over_nl80211: TRUE if userspace expects to exchange control
 *  port frames over NL80211 instead of the network interface.
 * @userspace_handles_dfs: whether user space controls DFS operation, i.e.
 *  changes the channel when a radar is detected. This is required
 *  to operate on DFS channels.
 * @basic_rates: bitmap of basic rates to use when creating the IBSS
 * @mcast_rate: per-band multicast rate index + 1 (0: disabled)
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *  will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 * @wep_keys: static WEP keys, if not NULL points to an array of
 *  CFG80211_MAX_WEP_KEYS WEP keys
 * @wep_tx_key: key index (0..3) of the default TX static WEP key
 */

struct cfg80211_ibss_params
{
  const uint8_t *ssid;
  const uint8_t *bssid;
  struct cfg80211_chan_def chandef;
  const uint8_t *ie;
  uint8_t ssid_len;
  uint8_t ie_len;
  uint16_t beacon_interval;
  uint32_t basic_rates;
  bool channel_fixed;
  bool privacy;
  bool control_port;
  bool control_port_over_nl80211;
  bool userspace_handles_dfs;
  int mcast_rate[NUM_NL80211_BANDS];
  struct ieee80211_ht_cap ht_capa;
  struct ieee80211_ht_cap ht_capa_mask;
  struct key_params *wep_keys;
  int wep_tx_key;
};

/**
 * struct cfg80211_bss_selection - connection parameters for BSS selection.
 *
 * @behaviour: requested BSS selection behaviour.
 * @param: parameters for requestion behaviour.
 * @band_pref: preferred band for %NL80211_BSS_SELECT_ATTR_BAND_PREF.
 * @adjust: parameters for %NL80211_BSS_SELECT_ATTR_RSSI_ADJUST.
 */

struct cfg80211_bss_selection
{
  enum nl80211_bss_select_attr behaviour;
  union
  {
    enum nl80211_band band_pref;
    struct cfg80211_bss_select_adjust adjust;
  } param;
};

/**
 * struct cfg80211_connect_params - Connection parameters
 *
 * This structure provides information needed to complete IEEE 802.11
 * authentication and association.
 *
 * @channel: The channel to use or %NULL if not specified (auto-select based
 *  on scan results)
 * @channel_hint: The channel of the recommended BSS for initial connection
 *  or %NULL if not specified
 * @bssid: The AP BSSID or %NULL if not specified (auto-select based on scan
 *  results)
 * @bssid_hint: The recommended AP BSSID for initial connection to the BSS or
 *  %NULL if not specified. Unlike the @bssid parameter, the driver is
 *  allowed to ignore this @bssid_hint if it has knowledge of a better BSS
 *  to use.
 * @ssid: SSID
 * @ssid_len: Length of ssid in octets
 * @auth_type: Authentication type (algorithm)
 * @ie: IEs for association request
 * @ie_len: Length of assoc_ie in octets
 * @privacy: indicates whether privacy-enabled APs should be used
 * @mfp: indicate whether management frame protection is used
 * @crypto: crypto settings
 * @key_len: length of WEP key for shared key authentication
 * @key_idx: index of WEP key for shared key authentication
 * @key: WEP key for shared key authentication
 * @flags:  See &enum cfg80211_assoc_req_flags
 * @bg_scan_period:  Background scan period in seconds
 *  or -1 to indicate that default value is to be used.
 * @ht_capa:  HT Capabilities over-rides.  Values set in ht_capa_mask
 *  will be used in ht_capa.  Un-supported values will be ignored.
 * @ht_capa_mask:  The bits of ht_capa which are to be used.
 * @vht_capa:  VHT Capability overrides
 * @vht_capa_mask: The bits of vht_capa which are to be used.
 * @pbss: if set, connect to a PCP instead of AP. Valid for DMG
 *  networks.
 * @bss_select: criteria to be used for BSS selection.
 * @prev_bssid: previous BSSID, if not %NULL use reassociate frame. This
 *  is used to indicate a request to reassociate within the ESS instead
 *  of a request to do the initial association with the ESS. When included,
 *  this is set to the BSSID of the current association, i.e., to the value
 *  that is included in the Current AP address field of the Reassociation
 *  Request frame.
 * @fils_erp_username: EAP re-authentication protocol (ERP) username part
 *  of the NAI or %NULL if not specified. This is used to construct FILS
 *  wrapped data IE.
 * @fils_erp_username_len: Length of @fils_erp_username in octets.
 * @fils_erp_realm: EAP re-authentication protocol (ERP) realm part of NAI or
 *  %NULL if not specified. This specifies the domain name of ER server and
 *  is used to construct FILS wrapped data IE.
 * @fils_erp_realm_len: Length of @fils_erp_realm in octets.
 * @fils_erp_next_seq_num: The next sequence number to use in the FILS ERP
 *  messages. This is also used to construct FILS wrapped data IE.
 * @fils_erp_rrk: ERP re-authentication Root Key (rRK) used to derive
 *  additional keys in FILS or %NULL if not specified.
 * @fils_erp_rrk_len: Length of @fils_erp_rrk in octets.
 * @want_1x: indicates user-space supports and wants to use 802.1X driver
 *  offload of 4-way handshake.
 * @edmg: define the EDMG channels.
 *  This may specify multiple channels and bonding options for the driver
 *  to choose from, based on BSS configuration.
 */

struct cfg80211_connect_params
{
  struct ieee80211_channel *channel;
  struct ieee80211_channel *channel_hint;
  const uint8_t *bssid;
  const uint8_t *bssid_hint;
  const uint8_t *ssid;
  size_t ssid_len;
  enum nl80211_auth_type auth_type;
  const uint8_t *ie;
  size_t ie_len;
  bool privacy;
  enum nl80211_mfp mfp;
  struct cfg80211_crypto_settings crypto;
  const uint8_t *key;
  uint8_t key_len;
  uint8_t key_idx;
  uint32_t flags;
  int bg_scan_period;
  struct ieee80211_ht_cap ht_capa;
  struct ieee80211_ht_cap ht_capa_mask;
  struct ieee80211_vht_cap vht_capa;
  struct ieee80211_vht_cap vht_capa_mask;
  bool pbss;
  struct cfg80211_bss_selection bss_select;
  const uint8_t *prev_bssid;
  const uint8_t *fils_erp_username;
  size_t fils_erp_username_len;
  const uint8_t *fils_erp_realm;
  size_t fils_erp_realm_len;
  uint16_t fils_erp_next_seq_num;
  const uint8_t *fils_erp_rrk;
  size_t fils_erp_rrk_len;
  bool want_1x;
  struct ieee80211_edmg edmg;
};

/**
 * struct cfg80211_update_ft_ies_params - FT IE Information
 *
 * This structure provides information needed to update the fast
 *  transition IE
 *
 * @md: The Mobility Domain ID, 2 Octet value
 * @ie: Fast Transition IEs
 * @ie_len: Length of ft_ie in octets
 */

struct cfg80211_update_ft_ies_params
{
  uint16_t md;
  const uint8_t *ie;
  size_t ie_len;
};

/**
 * struct cfg80211_mgmt_tx_params - mgmt tx parameters
 *
 * This structure provides information needed to transmit a mgmt frame
 *
 * @chan: channel to use
 * @offchan: indicates wether off channel operation is required
 * @wait: duration for ROC
 * @buf: buffer to transmit
 * @len: buffer length
 * @no_cck: don't use cck rates for this frame
 * @dont_wait_for_ack: tells the low level not to wait for an ack
 * @n_csa_offsets: length of csa_offsets array
 * @csa_offsets: array of all the csa offsets in the frame
 * @link_id: for MLO, the link ID to transmit on, -1 if not given; note
 *  that the link ID isn't validated (much), it's in range but the
 *  link might not exist (or be used by the receiver STA)
 */

struct cfg80211_mgmt_tx_params
{
  struct ieee80211_channel *chan;
  bool offchan;
  unsigned int wait;
  const uint8_t *buf;
  size_t len;
  bool no_cck;
  bool dont_wait_for_ack;
  int n_csa_offsets;
  const uint16_t *csa_offsets;
  int link_id;
};

/**
 * struct cfg80211_dscp_exception - DSCP exception
 *
 * @dscp: DSCP value that does not adhere to the user priority range
 *  definition
 * @up: user priority value to which the corresponding DSCP value belongs
 */

struct cfg80211_dscp_exception
{
  uint8_t dscp;
  uint8_t up;
};
/**
 * struct cfg80211_dscp_range - DSCP range definition for user priority
 *
 * @low: lowest DSCP value of this user priority range, inclusive
 * @high: highest DSCP value of this user priority range, inclusive
 */

struct cfg80211_dscp_range
{
  uint8_t low;
  uint8_t high;
};

/* QoS Map Set element length defined in IEEE Std 802.11-2012, 8.4.2.97 */
#define IEEE80211_QOS_MAP_MAX_EX	21
#define IEEE80211_QOS_MAP_LEN_MIN	16
#define IEEE80211_QOS_MAP_LEN_MAX \
  (IEEE80211_QOS_MAP_LEN_MIN + 2 * IEEE80211_QOS_MAP_MAX_EX)

/**
 * struct cfg80211_qos_map - QoS Map Information
 *
 * This struct defines the Interworking QoS map setting for DSCP values
 *
 * @num_des: number of DSCP exceptions (0..21)
 * @dscp_exception: optionally up to maximum of 21 DSCP exceptions from
 *  the user priority DSCP range definition
 * @up: DSCP range definition for a particular user priority
 */

struct cfg80211_qos_map
{
  uint8_t num_des;
  struct cfg80211_dscp_exception dscp_exception[IEEE80211_QOS_MAP_MAX_EX];
  struct cfg80211_dscp_range up[8];
};

/**
 * struct cfg80211_pmk_conf - PMK configuration
 *
 * @aa: authenticator address
 * @pmk_len: PMK length in bytes.
 * @pmk: the PMK material
 * @pmk_r0_name: PMK-R0 Name. NULL if not applicable (i.e., the PMK
 *  is not PMK-R0). When pmk_r0_name is not NULL, the pmk field
 *  holds PMK-R0.
 */

struct cfg80211_pmk_conf
{
  const uint8_t *aa;
  uint8_t pmk_len;
  const uint8_t *pmk;
  const uint8_t *pmk_r0_name;
};

/**
 * struct cfg80211_external_auth_params - Trigger External authentication.
 *
 * Commonly used across the external auth request and event interfaces.
 *
 * @action: action type / trigger for external authentication. Only
 *  significant for the authentication request event interface (driver to
 *  user space).
 * @bssid: BSSID of the peer with which the authentication has
 *  to happen. Used by both the authentication request event and
 *  authentication response command interface.
 * @ssid: SSID of the AP.  Used by both the authentication request event and
 *  authentication response command interface.
 * @key_mgmt_suite: AKM suite of the respective authentication. Used by the
 *  authentication request event interface.
 * @status: status code, %WLAN_STATUS_SUCCESS for successful authentication,
 *  use %WLAN_STATUS_UNSPECIFIED_FAILURE if user space cannot give you
 *  the real status code for failures. Used only for the authentication
 *  response command interface (user space to driver).
 * @pmkid: The identifier to refer a PMKSA.
 */

struct cfg80211_external_auth_params
{
  enum nl80211_external_auth_action action;
  uint8_t bssid[ETH_ALEN] __attribute__((aligned(2)));
  struct cfg80211_ssid ssid;
  unsigned int key_mgmt_suite;
  uint16_t status;
  const uint8_t *pmkid;
};

/**
 * struct mgmt_frame_regs - management frame registrations data
 * @global_stypes: bitmap of management frame subtypes registered
 *  for the entire device
 * @interface_stypes: bitmap of management frame subtypes registered
 *  for the given interface
 * @global_mcast_stypes: mcast RX is needed globally for these subtypes
 * @interface_mcast_stypes: mcast RX is needed on this interface
 *  for these subtypes
 */

struct mgmt_frame_regs
{
  uint32_t global_stypes;
  uint32_t interface_stypes;
  uint32_t global_mcast_stypes;
  uint32_t interface_mcast_stypes;
};

/**
 * struct cfg80211_nan_conf - NAN configuration
 *
 * @master_pref: master preference (1-254, 0 and 255 reserved)
 * @bands: operating bands, a bitmap of &enum nl80211_band values
 * @cluster_id: cluster ID to join or create
 * @enable_dw_notification: enable DW notification
 */

struct cfg80211_nan_conf
{
  uint8_t master_pref;
  uint8_t bands;
  const uint8_t *cluster_id;
  bool enable_dw_notification;
};

/**
 * enum cfg80211_nan_conf_changes - NAN configuration changed flags
 *
 * @CFG80211_NAN_CONF_CHANGED_PREF: master preference changed
 * @CFG80211_NAN_CONF_CHANGED_BANDS: operating bands changed
 */

enum cfg80211_nan_conf_changes
{
  CFG80211_NAN_CONF_CHANGED_PREF  = BIT(0),
  CFG80211_NAN_CONF_CHANGED_BANDS = BIT(1),
};

/**
 * struct cfg80211_ops - backend description for wireless configuration
 *
 * This struct is registered by fullmac card drivers and/or wireless stacks
 * in order to handle configuration requests on their interfaces.
 *
 * All callbacks except where otherwise noted should return 0
 * on success or a negative error code.
 *
 * All operations are invoked with the wiphy mutex held. The RTNL may be
 * held in addition (due to wireless extensions) but this cannot be relied
 * upon except in cases where documented below. Note that due to ordering,
 * the RTNL also cannot be acquired in any handlers.
 *
 * @add_virtual_intf: create a new virtual interface with the given name,
 *  must set the struct wireless_dev's iftype. Beware: You must create
 *  the new netdev in the wiphy's network namespace! Returns the struct
 *  wireless_dev, or an ERR_PTR. For P2P device wdevs, the driver must
 *  also set the address member in the wdev.
 *  This additionally holds the RTNL to be able to do netdev changes.
 *
 * @del_virtual_intf: remove the virtual interface
 *  This additionally holds the RTNL to be able to do netdev changes.
 *
 * @change_virtual_intf: change type/configuration of virtual interface,
 *  keep the struct wireless_dev's iftype updated.
 *  This additionally holds the RTNL to be able to do netdev changes.
 *
 * @add_intf_link: Add a new MLO link to the given interface. Note that
 *  the wdev->link[] data structure has been updated, so the new link
 *  address is available.
 * @del_intf_link: Remove an MLO link from the given interface.
 *
 * @add_key: add a key with the given parameters. @mac_addr will be %NULL
 *  when adding a group key. @link_id will be -1 for non-MLO connection.
 *  For MLO connection, @link_id will be >= 0 for group key and -1 for
 *  pairwise key, @mac_addr will be peer's MLD address for MLO pairwise key.
 *
 * @get_key: get information about the key with the given parameters.
 *  @mac_addr will be %NULL when requesting information for a group
 *  key. All pointers given to the @callback function need not be valid
 *  after it returns. This function should return an error if it is
 *  not possible to retrieve the key, -ENOENT if it doesn't exist.
 *  @link_id will be -1 for non-MLO connection. For MLO connection,
 *  @link_id will be >= 0 for group key and -1 for pairwise key, @mac_addr
 *  will be peer's MLD address for MLO pairwise key.
 *
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *  and @key_index, return -ENOENT if the key doesn't exist. @link_id will
 *  be -1 for non-MLO connection. For MLO connection, @link_id will be >= 0
 *  for group key and -1 for pairwise key, @mac_addr will be peer's MLD
 *  address for MLO pairwise key.
 *
 * @set_default_key: set the default key on an interface. @link_id will
 *  be >= 0 for MLO connection and -1 for non-MLO connection.
 *
 * @set_default_mgmt_key: set the default management frame key on an
 *  interface. @link_id will be >= 0 for MLO connection and -1 for
 *  non-MLO connection.
 *
 * @set_default_beacon_key: set the default Beacon frame key on an interface.
 *  @link_id will be >= 0 for MLO connection and -1 for non-MLO connection.
 *
 * @add_station: Add a new station.
 * @del_station: Remove a station
 * @change_station: Modify a given station. Note that flags changes are
 *  not much validated in cfg80211, in particular the auth/assoc/authorized
 *  flags might come to the driver in invalid combinations -- make sure to
 *  check them, also against the existing state! Drivers must call
 *  cfg80211_check_station_change() to validate the information.
 * @get_station: get station information for the station identified by @mac
 * @dump_station: dump station callback -- resume dump at index @idx
 *
 * @set_txq_params: Set TX queue parameters
 *
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *  the driver, and will be valid until passed to cfg80211_scan_done().
 *  For scan results, call cfg80211_inform_bss(); you can call this outside
 *  the scan/scan_done bracket too.
 * @abort_scan: Tell the driver to abort an ongoing scan. The driver shall
 *  indicate the status of the scan through cfg80211_scan_done().
 *
 * @auth: Request to authenticate with the specified peer
 *  (invoked with the wireless_dev mutex held)
 * @assoc: Request to (re)associate with the specified peer
 *  (invoked with the wireless_dev mutex held)
 * @deauth: Request to deauthenticate from the specified peer
 *  (invoked with the wireless_dev mutex held)
 * @disassoc: Request to disassociate from the specified peer
 *  (invoked with the wireless_dev mutex held)
 *
 * @connect: Connect to the ESS with the specified parameters. When
 *  connected, call cfg80211_connect_result()/cfg80211_connect_bss() with
 *  status code %WLAN_STATUS_SUCCESS. If the connection fails for some
 *  reason, call cfg80211_connect_result()/cfg80211_connect_bss() with the
 *  status code from the AP or cfg80211_connect_timeout() if no frame with
 *  status code was received. The driver is allowed to roam to other BSSes
 *  within the ESS when the other BSS matches the connect parameters. When
 *  such roaming is initiated by the driver, the driver is expected to verify
 *  that the target matches the configured security parameters and to use
 *  Reassociation Request
 *  frame instead of Association Request frame.
 *  The connect function can also be used to request the driver to perform a
 *  specific roam when connected to an ESS. In that case, the prev_bssid
 *  parameter is set to the BSSID of the currently associated BSS as an
 *  indication of requesting reassociation.
 *  In both the driver-initiated and new connect() call initiated roaming
 *  cases, the result of roaming is indicated with a call to
 *  cfg80211_roamed(). (invoked with the wireless_dev mutex held)
 * @update_connect_params: Update the connect parameters while connected to a
 *  BSS. The updated parameters can be used by driver/firmware for
 *  subsequent BSS selection (roaming) decisions and to form the
 *  Authentication/(Re)Association Request frames. This call does not
 *  request an immediate disassociation or reassociation with the current
 *  BSS, i.e., this impacts only subsequent (re)associations. The bits in
 *  changed are defined in &enum cfg80211_connect_params_changed.
 *  (invoked with the wireless_dev mutex held)
 * @disconnect: Disconnect from the BSS/ESS or stop connection attempts if
 *     connection is in progress. Once done, call cfg80211_disconnected() in
 *     case connection was already established (invoked with the
 *     wireless_dev mutex held), otherwise call cfg80211_connect_timeout().
 *
 * @join_ibss: Join the specified IBSS (or create if necessary). Once done,
 *  call cfg80211_ibss_joined(), also call that function when changing BSSID
 *  due to a merge.
 *  (invoked with the wireless_dev mutex held)
 * @leave_ibss: Leave the IBSS.
 *  (invoked with the wireless_dev mutex held)
 *
 * @remain_on_channel: Request the driver to remain awake on the specified
 *  channel for the specified duration to complete an off-channel
 *  operation (e.g., public action frame exchange). When the driver is
 *  ready on the requested channel, it must indicate this with an event
 *  notification by calling cfg80211_ready_on_channel().
 * @cancel_remain_on_channel: Cancel an on-going remain-on-channel operation.
 *  This allows the operation to be terminated prior to timeout based on
 *  the duration value.
 * @mgmt_tx: Transmit a management frame.
 * @mgmt_tx_cancel_wait: Cancel the wait time from transmitting a management
 *  frame on another channel
 *
 * @set_bitrate_mask: set the bitrate mask configuration
 *
 * @sched_scan_start: Tell the driver to start a scheduled scan.
 * @sched_scan_stop: Tell the driver to stop an ongoing scheduled scan with
 *  given request id. This call must stop the scheduled scan and be ready
 *  for starting a new one before it returns, i.e. @sched_scan_start may be
 *  called immediately after that again and should not fail in that case.
 *  The driver should not call cfg80211_sched_scan_stopped() for a requested
 *  stop (when this method returns 0).
 *
 * @update_mgmt_frame_registrations: Notify the driver that management frame
 *  registrations were updated. The callback is allowed to sleep.
 *
 * @set_antenna: Set antenna configuration (tx_ant, rx_ant) on the device.
 *  Parameters are bitmaps of allowed antennas to use for TX/RX. Drivers may
 *  reject TX/RX mask combinations they cannot support by returning -EINVAL
 *  (also see nl80211.h @NL80211_ATTR_WIPHY_ANTENNA_TX).
 *
 * @get_antenna: Get current antenna configuration from device
 *  (tx_ant, rx_ant).
 *
 * @probe_client: probe an associated client, must return a cookie that it
 *  later passes to cfg80211_probe_status().
 *
 * @start_radar_detection: Start radar detection in the driver.
 *
 * @update_ft_ies: Provide updated Fast BSS Transition information to the
 *  driver. If the SME is in the driver/firmware, this information can be
 *  used in building Authentication and Reassociation Request frames.
 *
 * @channel_switch: initiate channel-switch procedure (with CSA). Driver is
 *  responsible for veryfing if the switch is possible. Since this is
 *  inherently tricky driver may decide to disconnect an interface later
 *  with cfg80211_stop_iface(). This doesn't mean driver can accept
 *  everything. It should do it's best to verify requests and reject them
 *  as soon as possible.
 *
 * @set_qos_map: Set QoS mapping information to the driver
 *
 * @get_txq_stats: Get TXQ stats for interface or phy. If wdev is %NULL, this
 *     function should return phy stats, and interface stats otherwise.
 *
 * @set_pmk: configure the PMK to be used for offloaded 802.1X 4-Way
 *  handshake. If not deleted through @del_pmk the PMK remains valid
 *  until disconnect upon which the driver should clear it.
 *  (invoked with the wireless_dev mutex held)
 * @del_pmk: delete the previously configured PMK for the given
 *  authenticator.
 *  (invoked with the wireless_dev mutex held)
 *
 * @external_auth: indicates result of offloaded authentication processing
 *  from user space
 *
 * @set_radar_background: Configure dedicated offchannel chain available for
 *  radar/CAC detection on some hw. This chain can't be used to transmit
 *  or receive frames and it is bounded to a running wdev.
 *  Background radar/CAC detection allows to avoid the CAC downtime
 *  switching to a different channel during CAC detection on the selected
 *  radar channel.
 *  The caller is expected to set chandef pointer to NULL in order to
 *  disable background CAC/radar detection.
 * @add_link_station: Add a link to a station.
 * @mod_link_station: Modify a link of a station.
 * @del_link_station: Remove a link of a station.
 */

struct cfg80211_ops
{
  struct wireless_dev *(*add_virtual_intf)(struct wiphy *wiphy,
                                          const char *name,
                                          unsigned char name_assign_type,
                                          enum nl80211_iftype type,
                                          struct vif_params *params);
  int (*del_virtual_intf)(struct wiphy *wiphy,
                          struct wireless_dev *wdev);
  int (*change_virtual_intf)(struct wiphy *wiphy,
                             struct net_driver_s *dev,
                             enum nl80211_iftype type,
                             struct vif_params *params);

  int (*add_intf_link)(struct wiphy *wiphy,
                       struct wireless_dev *wdev,
                       unsigned int link_id);
  void (*del_intf_link)(struct wiphy *wiphy,
                        struct wireless_dev *wdev,
                        unsigned int link_id);

  int (*add_key)(struct wiphy *wiphy, struct net_driver_s *netdev,
                 int link_id, uint8_t key_index, bool pairwise,
                 const uint8_t *mac_addr, struct key_params *params);
  int (*get_key)(struct wiphy *wiphy, struct net_driver_s *netdev,
                 int link_id, uint8_t key_index, bool pairwise,
                 const uint8_t *mac_addr, void *cookie,
                 void (*callback)(void *cookie, struct key_params *));
  int (*del_key)(struct wiphy *wiphy, struct net_driver_s *netdev,
                 int link_id, uint8_t key_index, bool pairwise,
                 const uint8_t *mac_addr);
  int (*set_default_key)(struct wiphy *wiphy,
                         struct net_driver_s *netdev, int link_id,
                         uint8_t key_index, bool unicast, bool multicast);
  int (*set_default_mgmt_key)(struct wiphy *wiphy,
                              struct net_driver_s *netdev, int link_id,
                              uint8_t key_index);
  int (*set_default_beacon_key)(struct wiphy *wiphy,
                                struct net_driver_s *netdev,
                                int link_id,
                                uint8_t key_index);

  int (*start_ap)(struct wiphy *wiphy, struct net_driver_s *netdev,
                  struct cfg80211_ap_settings *settings);
  int (*change_beacon)(struct wiphy *wiphy, struct net_driver_s *netdev,
                       struct cfg80211_beacon_data *info);
  int (*stop_ap)(struct wiphy *wiphy, struct net_driver_s *netdev,
                 unsigned int link_id);

  int (*add_station)(struct wiphy *wiphy, struct net_driver_s *dev,
                     const uint8_t *mac,
                     struct station_parameters *params);
  int (*del_station)(struct wiphy *wiphy, struct net_driver_s *dev,
                     struct station_del_parameters *params);
  int (*change_station)(struct wiphy *wiphy, struct net_driver_s *dev,
                        const uint8_t *mac,
                        struct station_parameters *params);
  int (*get_station)(struct wiphy *wiphy, struct net_driver_s *dev,
                     const uint8_t *mac, struct station_info *sinfo);
  int (*dump_station)(struct wiphy *wiphy, struct net_driver_s *dev,
                      int idx, uint8_t *mac, struct station_info *sinfo);

  int (*set_txq_params)(struct wiphy *wiphy, struct net_driver_s *dev,
                        struct ieee80211_txq_params *params);

  int (*scan)(struct wiphy *wiphy,
              struct cfg80211_scan_request *request);
  void (*abort_scan)(struct wiphy *wiphy, struct wireless_dev *wdev);

  int (*auth)(struct wiphy *wiphy, struct net_driver_s *dev,
              struct cfg80211_auth_request *req);
  int (*assoc)(struct wiphy *wiphy, struct net_driver_s *dev,
               struct cfg80211_assoc_request *req);
  int (*deauth)(struct wiphy *wiphy, struct net_driver_s *dev,
                struct cfg80211_deauth_request *req);
  int (*disassoc)(struct wiphy *wiphy, struct net_driver_s *dev,
                  struct cfg80211_disassoc_request *req);

  int (*connect)(struct wiphy *wiphy, struct net_driver_s *dev,
                 struct cfg80211_connect_params *sme);
  int (*update_connect_params)(struct wiphy *wiphy,
                               struct net_driver_s *dev,
                               struct cfg80211_connect_params *sme,
                               uint32_t changed);
  int (*disconnect)(struct wiphy *wiphy, struct net_driver_s *dev,
                    uint16_t reason_code);

  int (*join_ibss)(struct wiphy *wiphy, struct net_driver_s *dev,
                   struct cfg80211_ibss_params *params);
  int (*leave_ibss)(struct wiphy *wiphy, struct net_driver_s *dev);

  int (*set_tx_power)(struct wiphy *wiphy, struct wireless_dev *wdev,
                      enum nl80211_tx_power_setting type, int mbm);
  int (*set_bitrate_mask)(struct wiphy *wiphy,
                          struct net_driver_s *dev,
                          unsigned int link_id,
                          const uint8_t *peer,
                          const struct cfg80211_bitrate_mask *mask);

  int (*remain_on_channel)(struct wiphy *wiphy,
                           struct wireless_dev *wdev,
                           struct ieee80211_channel *chan,
                           unsigned int duration,
                           uint64_t *cookie);
  int (*cancel_remain_on_channel)(struct wiphy *wiphy,
                                  struct wireless_dev *wdev,
                                  uint64_t cookie);

  int (*mgmt_tx)(struct wiphy *wiphy, struct wireless_dev *wdev,
                 struct cfg80211_mgmt_tx_params *params,
                 uint64_t *cookie);
  int (*mgmt_tx_cancel_wait)(struct wiphy *wiphy,
                             struct wireless_dev *wdev,
                             uint64_t cookie);

  void (*update_mgmt_frame_registrations)(struct wiphy *wiphy,
                                         struct wireless_dev *wdev,
                                         struct mgmt_frame_regs *upd);

  int (*sched_scan_start)(struct wiphy *wiphy,
                          struct net_driver_s *dev,
                          struct cfg80211_sched_scan_request *request);
  int (*sched_scan_stop)(struct wiphy *wiphy, struct net_driver_s *dev,
                         uint64_t reqid);
  int (*set_antenna)(struct wiphy *wiphy, uint32_t tx_ant, uint32_t rx_ant);
  int (*probe_client)(struct wiphy *wiphy, struct net_driver_s *dev,
                      const uint8_t *peer, uint64_t *cookie);

  int (*start_radar_detection)(struct wiphy *wiphy,
                               struct net_driver_s *dev,
                               struct cfg80211_chan_def *chandef,
                               uint32_t cac_time_ms);

  int (*update_ft_ies)(struct wiphy *wiphy, struct net_driver_s *dev,
                       struct cfg80211_update_ft_ies_params *ftie);

  int (*channel_switch)(struct wiphy *wiphy,
                        struct net_driver_s *dev,
                        struct cfg80211_csa_settings *params);

  int (*set_qos_map)(struct wiphy *wiphy,
                     struct net_driver_s *dev,
                     struct cfg80211_qos_map *qos_map);

  int (*set_ap_chanwidth)(struct wiphy *wiphy, struct net_driver_s *dev,
                          unsigned int link_id,
                          struct cfg80211_chan_def *chandef);

  int (*get_txq_stats)(struct wiphy *wiphy,
                       struct wireless_dev *wdev,
                       struct cfg80211_txq_stats *txqstats);

  int (*set_pmk)(struct wiphy *wiphy, struct net_driver_s *dev,
                 const struct cfg80211_pmk_conf *conf);
  int (*del_pmk)(struct wiphy *wiphy, struct net_driver_s *dev,
                 const uint8_t *aa);
  int (*external_auth)(struct wiphy *wiphy, struct net_driver_s *dev,
                       struct cfg80211_external_auth_params *params);

  int (*tx_control_port)(struct wiphy *wiphy,
                         struct net_driver_s *dev,
                         const uint8_t *buf, size_t len,
                         const uint8_t *dest, const uint16_t proto,
                         const bool noencrypt, int link_id,
                         uint64_t *cookie);

  int (*set_radar_background)(struct wiphy *wiphy,
                              struct cfg80211_chan_def *chandef);
  int (*add_link_station)(struct wiphy *wiphy, struct net_driver_s *dev,
                          struct link_station_parameters *params);
  int (*mod_link_station)(struct wiphy *wiphy, struct net_driver_s *dev,
                          struct link_station_parameters *params);
  int (*del_link_station)(struct wiphy *wiphy, struct net_driver_s *dev,
                          struct link_station_del_parameters *params);

  int (*start_nan)(struct wiphy *wiphy, struct wireless_dev *wdev,
                   struct cfg80211_nan_conf *conf);
  int (*stop_nan)(struct wiphy *wiphy, struct wireless_dev *wdev);
  int (*nan_change_conf)(struct wiphy *wiphy, struct wireless_dev *wdev,
                         struct cfg80211_nan_conf *conf,
                         uint32_t changes);

  int (*probe_req_report)(struct wiphy *wiphy,
                          struct wireless_dev *wdev,
                          int report);
};

/**
 * enum wiphy_flags - wiphy capability flags
 *
 * @WIPHY_FLAG_SPLIT_SCAN_6GHZ: if set to true, the scan request will
 *  be split into two, first for legacy bands and second for UHB.
 * @WIPHY_FLAG_NETNS_OK: if not set, do not allow changing the netns of this
 *  wiphy at all
 * @WIPHY_FLAG_PS_ON_BY_DEFAULT: if set to true, powersave will be enabled
 *  by default -- this flag will be set depending on the kernel's default
 *  on wiphy_new(), but can be changed by the driver if it has a good
 *  reason to override the default
 * @WIPHY_FLAG_4ADDR_AP: supports 4addr mode even on AP (with a
 *  single station on a VLAN interface). This flag also serves an extra
 *  purpose of supporting 4ADDR AP mode on devices which do not support
 *  AP/VLAN iftype.
 * @WIPHY_FLAG_4ADDR_STATION: supports 4addr mode even as a station
 * @WIPHY_FLAG_CONTROL_PORT_PROTOCOL: This device supports setting the
 *  control port protocol ethertype. The device also honours the
 *  control_port_no_encrypt flag.
 * @WIPHY_FLAG_IBSS_RSN: The device supports IBSS RSN.
 * @WIPHY_FLAG_MESH_AUTH: The device supports mesh authentication by routing
 *  auth frames to userspace. See @NL80211_MESH_SETUP_USERSPACE_AUTH.
 * @WIPHY_FLAG_SUPPORTS_FW_ROAM: The device supports roaming feature in the
 *  firmware.
 * @WIPHY_FLAG_AP_UAPSD: The device supports uapsd on AP.
 * @WIPHY_FLAG_SUPPORTS_TDLS: The device supports TDLS (802.11z) operation.
 * @WIPHY_FLAG_TDLS_EXTERNAL_SETUP: The device does not handle TDLS (802.11z)
 *  link setup/discovery operations internally. Setup, discovery and
 *  teardown packets should be sent through the @NL80211_CMD_TDLS_MGMT
 *  command. When this flag is not set, @NL80211_CMD_TDLS_OPER should be
 *  used for asking the driver/firmware to perform a TDLS operation.
 * @WIPHY_FLAG_HAVE_AP_SME: device integrates AP SME
 * @WIPHY_FLAG_REPORTS_OBSS: the device will report beacons from other BSSes
 *  when there are virtual interfaces in AP mode by calling
 *  cfg80211_report_obss_beacon().
 * @WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD: When operating as an AP, the device
 *  responds to probe-requests in hardware.
 * @WIPHY_FLAG_OFFCHAN_TX: Device supports direct off-channel TX.
 * @WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL: Device supports remain-on-channel call.
 * @WIPHY_FLAG_SUPPORTS_5_10_MHZ: Device supports 5 MHz and 10 MHz channels.
 * @WIPHY_FLAG_HAS_CHANNEL_SWITCH: Device supports channel switch in
 *  beaconing mode (AP, IBSS, Mesh, ...).
 * @WIPHY_FLAG_HAS_STATIC_WEP: The device supports static WEP key
 *  installation before connection.
 * @WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK: The device supports bigger kek and
 *  kck keys.
 * @WIPHY_FLAG_SUPPORTS_MLO: This is a temporary flag gating the MLO APIs,
 *  in order to not have them reachable in normal drivers, until we have
 *  complete feature/interface combinations/etc. advertisement. No driver
 *  should set this flag for now.
 */

enum wiphy_flags
{
  WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK    = 1 << 0,
  WIPHY_FLAG_SUPPORTS_MLO            = 1 << 1,
  WIPHY_FLAG_SPLIT_SCAN_6GHZ         = 1 << 2,
  WIPHY_FLAG_NETNS_OK                = 1 << 3,
  WIPHY_FLAG_PS_ON_BY_DEFAULT        = 1 << 4,
  WIPHY_FLAG_4ADDR_AP                = 1 << 5,
  WIPHY_FLAG_4ADDR_STATION           = 1 << 6,
  WIPHY_FLAG_CONTROL_PORT_PROTOCOL   = 1 << 7,
  WIPHY_FLAG_IBSS_RSN                = 1 << 8,
  WIPHY_FLAG_MESH_AUTH               = 1 << 10,

  /* Reserved bit 11 */

  /* Reserved bit 12 */

  WIPHY_FLAG_SUPPORTS_FW_ROAM        = 1 << 13,
  WIPHY_FLAG_AP_UAPSD                = 1 << 14,
  WIPHY_FLAG_SUPPORTS_TDLS           = 1 << 15,
  WIPHY_FLAG_TDLS_EXTERNAL_SETUP     = 1 << 16,
  WIPHY_FLAG_HAVE_AP_SME             = 1 << 17,
  WIPHY_FLAG_REPORTS_OBSS            = 1 << 18,
  WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD   = 1 << 19,
  WIPHY_FLAG_OFFCHAN_TX              = 1 << 20,
  WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL   = 1 << 21,
  WIPHY_FLAG_SUPPORTS_5_10_MHZ       = 1 << 22,
  WIPHY_FLAG_HAS_CHANNEL_SWITCH      = 1 << 23,
  WIPHY_FLAG_HAS_STATIC_WEP          = 1 << 24,
};

/**
 * struct ieee80211_iface_limit - limit on certain interface types
 * @max: maximum number of interfaces of these types
 * @types: interface types (bits)
 */

struct ieee80211_iface_limit
{
  uint16_t max;
  uint16_t types;
};

/**
 * struct ieee80211_iface_combination - possible interface combination
 *
 * With this structure the driver can describe which interface
 * combinations it supports concurrently.
 *
 * Examples:
 *
 * 1. Allow #STA <= 1, #AP <= 1, matching BI, channels = 1, 2 total:
 *
 *   .. code-block:: c
 *
 *  struct ieee80211_iface_limit limits1[] = {
 *    { .max = 1, .types = BIT(NL80211_IFTYPE_STATION), },
 *    { .max = 1, .types = BIT(NL80211_IFTYPE_AP},
 *  };
 *  struct ieee80211_iface_combination combination1 = {
 *    .limits = limits1,
 *    .n_limits = ARRAY_SIZE(limits1),
 *    .max_interfaces = 2,
 *    .beacon_int_infra_match = true,
 *  };
 *
 *
 * 2. Allow #{AP, P2P-GO} <= 8, channels = 1, 8 total:
 *
 *   .. code-block:: c
 *
 *  struct ieee80211_iface_limit limits2[] = {
 *    { .max = 8, .types = BIT(NL80211_IFTYPE_AP) |
 *             BIT(NL80211_IFTYPE_P2P_GO), },
 *  };
 *  struct ieee80211_iface_combination combination2 = {
 *    .limits = limits2,
 *    .n_limits = ARRAY_SIZE(limits2),
 *    .max_interfaces = 8,
 *    .num_different_channels = 1,
 *  };
 *
 *
 * 3. Allow #STA <= 1, #{P2P-client,P2P-GO} <= 3 on two channels, 4 total.
 *
 *   This allows for an infrastructure connection and three P2P connections.
 *
 *   .. code-block:: c
 *
 *  struct ieee80211_iface_limit limits3[] = {
 *    { .max = 1, .types = BIT(NL80211_IFTYPE_STATION), },
 *    { .max = 3, .types = BIT(NL80211_IFTYPE_P2P_GO) |
 *             BIT(NL80211_IFTYPE_P2P_CLIENT), },
 *  };
 *  struct ieee80211_iface_combination combination3 = {
 *    .limits = limits3,
 *    .n_limits = ARRAY_SIZE(limits3),
 *    .max_interfaces = 4,
 *    .num_different_channels = 2,
 *  };
 *
 */

struct ieee80211_iface_combination
{
  const struct ieee80211_iface_limit *limits;
  uint32_t num_different_channels;
  uint16_t max_interfaces;
  uint8_t n_limits;
  bool beacon_int_infra_match;
  uint8_t radar_detect_widths;
  uint8_t radar_detect_regions;
  uint32_t beacon_int_min_gcd;
};

struct ieee80211_txrx_stypes
{
  uint16_t tx;
  uint16_t rx;
};

/**
 * struct wiphy_iftype_ext_capab - extended capabilities per interface type
 * @iftype: interface type
 * @extended_capabilities: extended capabilities supported by the driver,
 *  additional capabilities might be supported by userspace; these are the
 *  802.11 extended capabilities ("Extended Capabilities element") and are
 *  in the same format as in the information element. See IEEE Std
 *  802.11-2012 8.4.2.29 for the defined fields.
 * @extended_capabilities_mask: mask of the valid values
 * @extended_capabilities_len: length of the extended capabilities
 * @eml_capabilities: EML capabilities (for MLO)
 * @mld_capa_and_ops: MLD capabilities and operations (for MLO)
 */

struct wiphy_iftype_ext_capab
{
  enum nl80211_iftype iftype;
  const uint8_t *extended_capabilities;
  const uint8_t *extended_capabilities_mask;
  uint8_t extended_capabilities_len;
  uint16_t eml_capabilities;
  uint16_t mld_capa_and_ops;
};

/**
 * struct wiphy - wireless hardware description
 * @mtx: mutex for the data (structures) of this device
 * @reg_notifier: the driver's regulatory notification callback,
 *  note that if your driver uses wiphy_apply_custom_regulatory()
 *  the reg_notifier's request can be passed as NULL
 * @regd: the driver's regulatory domain, if one was requested via
 *  the regulatory_hint() API. This can be used by the driver
 *  on the reg_notifier() if it chooses to ignore future
 *  regulatory domain changes caused by other drivers.
 * @signal_type: signal type reported in &struct cfg80211_bss.
 * @cipher_suites: supported cipher suites
 * @n_cipher_suites: number of supported cipher suites
 * @akm_suites: supported AKM suites. These are the default AKMs supported if
 *  the supported AKMs not advertized for a specific interface type in
 *  iftype_akm_suites.
 * @n_akm_suites: number of supported AKM suites
 * @iftype_akm_suites: array of supported akm suites info per interface type.
 *  Note that the bits in @iftypes_mask inside this structure cannot
 *  overlap (i.e. only one occurrence of each type is allowed across all
 *  instances of iftype_akm_suites).
 * @num_iftype_akm_suites: number of interface types for which supported akm
 *  suites are specified separately.
 * @retry_short: Retry limit for short frames (dot11ShortRetryLimit)
 * @retry_long: Retry limit for long frames (dot11LongRetryLimit)
 * @frag_threshold: Fragmentation threshold (dot11FragmentationThreshold);
 *  -1 = fragmentation disabled, only odd values >= 256 used
 * @rts_threshold: RTS threshold (dot11RTSThreshold); -1 = RTS/CTS disabled
 * @_net: the network namespace this wiphy currently lives in
 * @perm_addr: permanent MAC address of this device
 * @addr_mask: If the device supports multiple MAC addresses by masking,
 *  set this to a mask with variable bits set to 1, e.g. if the last
 *  four bits are variable then set it to 00-00-00-00-00-0f. The actual
 *  variable bits shall be determined by the interfaces added, with
 *  interfaces not matching the mask being rejected to be brought up.
 * @n_addresses: number of addresses in @addresses.
 * @addresses: If the device has more than one address, set this pointer
 *  to a list of addresses (6 bytes each). The first one will be used
 *  by default for perm_addr. In this case, the mask should be set to
 *  all-zeroes. In this case it is assumed that the device can handle
 *  the same number of arbitrary MAC addresses.
 * @registered: protects ->resume and ->suspend sysfs callbacks against
 *  unregister hardware
 * @debugfsdir: debugfs directory used for this wiphy
 *  (ieee80211/<wiphyname>).
 *  It will be renamed automatically on wiphy renames.
 * @dev: (virtual) struct device for this wiphy. The item in
 *  /sys/class/ieee80211/ points to this. You need use set_wiphy_dev()
 *  (see below).
 * @wext: wireless extension handlers
 * @priv: driver private data (sized according to wiphy_new() parameter)
 * @interface_modes: bitmask of interfaces types valid for this wiphy,
 *  must be set by driver
 * @iface_combinations: Valid interface combinations array, should not
 *  list single interface types.
 * @n_iface_combinations: number of entries in @iface_combinations array.
 * @software_iftypes: bitmask of software interface types, these are not
 *  subject to any restrictions since they are purely managed in SW.
 * @flags: wiphy flags, see &enum wiphy_flags
 * @regulatory_flags: wiphy regulatory flags, see
 *  &enum ieee80211_regulatory_flags
 * @features: features advertised to nl80211, see
 *  &enum nl80211_feature_flags.
 * @ext_features: extended features advertised to nl80211, see
 *  &enum nl80211_ext_feature_index.
 * @iftype_ext_capab: extended capabilities per interface type
 * @n_iftype_ext_capab: number of entries in @iftype_ext_capab array
 * @bss_priv_size: each BSS struct has private data allocated with it,
 *  this variable determines its size
 * @max_scan_ssids: maximum number of SSIDs the device can scan for in
 *  any given scan
 * @max_sched_scan_reqs: maximum number of scheduled scan requests that
 *  the device can run concurrently.
 * @max_sched_scan_ssids: maximum number of SSIDs the device can scan
 *  for in any given scheduled scan
 * @max_match_sets: maximum number of match sets the device can handle
 *  when performing a scheduled scan, 0 if filtering is not
 *  supported.
 * @max_scan_ie_len: maximum length of user-controlled IEs device can
 *  add to probe request frames transmitted during a scan, must not
 *  include fixed IEs like supported rates
 * @max_sched_scan_ie_len: same as max_scan_ie_len, but for scheduled
 *  scans
 * @max_sched_scan_plans: maximum number of scan plans (scan interval,
 *  number of iterations) for scheduled scan supported by the device.
 * @max_sched_scan_plan_interval: maximum interval (in seconds) for a
 *  single scan plan supported by the device.
 * @max_sched_scan_plan_iterations: maximum number of iterations for a single
 *  scan plan supported by the device.
 * @coverage_class: current coverage class
 * @fw_version: firmware version for ethtool reporting
 * @hw_version: hardware version for ethtool reporting
 * @max_num_pmkids: maximum number of PMKIDs supported by device
 * @privid: a pointer that drivers can use to identify if an arbitrary
 *  wiphy is theirs, e.g. in global notifiers
 * @bands: information about bands/channels supported by this device
 *
 * @mgmt_stypes: bitmasks of frame subtypes that can be subscribed to or
 *  transmitted through nl80211, points to an array indexed by interface
 *  type
 *
 * @available_antennas_tx: bitmap of antennas which are available to be
 *  configured as TX antennas. Antenna configuration commands will be
 *  rejected unless this or @available_antennas_rx is set.
 *
 * @available_antennas_rx: bitmap of antennas which are available to be
 *  configured as RX antennas. Antenna configuration commands will be
 *  rejected unless this or @available_antennas_tx is set.
 *
 * @probe_resp_offload:
 *   Bitmap of supported protocols for probe response offloading.
 *   See &enum nl80211_probe_resp_offload_support_attr. Only valid
 *   when the wiphy flag @WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD is set.
 *
 * @max_remain_on_channel_duration: Maximum time a remain-on-channel
 *  duration may request, if implemented.
 *
 * @wowlan: WoWLAN support information
 * @wowlan_config: current WoWLAN configuration; this should usually not be
 *  used since access to it is necessarily racy, use the parameter passed
 *  to the suspend() operation instead.
 *
 * @ap_sme_capa: AP SME capabilities, flags from
 *  &enum nl80211_ap_sme_features.
 * @ht_capa_mod_mask:  Specify what ht_cap values can be over-ridden.
 *  If null, then none can be over-ridden.
 * @vht_capa_mod_mask:  Specify what VHT capabilities can be over-ridden.
 *  If null, then none can be over-ridden.
 *
 * @wdev_list: the list of associated (virtual) interfaces; this list must
 *  not be modified by the driver, but can be read with RTNL/RCU protection.
 *
 * @max_acl_mac_addrs: Maximum number of MAC addresses that the device
 *  supports for ACL.
 *
 * @extended_capabilities: extended capabilities supported by the driver,
 *  additional capabilities might be supported by userspace; these are
 *  the 802.11 extended capabilities ("Extended Capabilities element")
 *  and are in the same format as in the information element. See
 *  802.11-2012 8.4.2.29 for the defined fields. These are the default
 *  extended capabilities to be used if the capabilities are not specified
 *  for a specific interface type in iftype_ext_capab.
 * @extended_capabilities_mask: mask of the valid values
 * @extended_capabilities_len: length of the extended capabilities
 * @iftype_ext_capab: array of extended capabilities per interface type
 * @num_iftype_ext_capab: number of interface types for which extended
 *  capabilities are specified separately.
 * @coalesce: packet coalescing support information
 *
 * @vendor_commands: array of vendor commands supported by the hardware
 * @n_vendor_commands: number of vendor commands
 * @vendor_events: array of vendor events supported by the hardware
 * @n_vendor_events: number of vendor events
 *
 * @max_ap_assoc_sta: maximum number of associated stations supported
 *  in AP mode (including P2P GO) or 0 to indicate no such limit is
 *  advertised. The driver is allowed to advertise a theoretical limit
 *  that it can reach in some cases, but may not always reach.
 *
 * @max_num_csa_counters: Number of supported csa_counters in beacons
 *  and probe responses.  This value should be set if the driver
 *  wishes to limit the number of csa counters. Default (0) means
 *  infinite.
 * @bss_select_support: bitmask indicating the BSS selection criteria
 *  supported by the driver in the .connect() callback. The bit position maps
 *  to the attribute indices defined in &enum nl80211_bss_select_attr.
 *
 * @nan_supported_bands: bands supported by the device in NAN mode, a
 *  bitmap of &enum nl80211_band values.  For instance, for
 *  NL80211_BAND_2GHZ, bit 0 would be set
 *  (i.e. BIT(NL80211_BAND_2GHZ)).
 *
 * @txq_limit: configuration of internal TX queue frame limit
 * @txq_memory_limit: configuration internal TX queue memory limit
 * @txq_quantum: configuration of internal TX queue scheduler quantum
 *
 * @tx_queue_len: allow setting transmit queue len for drivers not using
 *  wake_tx_queue
 *
 * @support_mbssid: can HW support association with nontransmitted AP
 * @support_only_he_mbssid: don't parse MBSSID elements if it is not
 *  HE AP, in order to avoid compatibility issues.
 *  @support_mbssid must be set for this to have any effect.
 *
 * @pmsr_capa: peer measurement capabilities
 *
 * @tid_config_support: describes the per-TID config support that the
 *  device has
 * @tid_config_support.vif: bitmap of attributes (configurations)
 *  supported by the driver for each vif
 * @tid_config_support.peer: bitmap of attributes (configurations)
 *  supported by the driver for each peer
 * @tid_config_support.max_retry: maximum supported retry count for
 *  long/short retry configuration
 *
 * @max_data_retry_count: maximum supported per TID retry count for
 *  configuration through the %NL80211_TID_CONFIG_ATTR_RETRY_SHORT and
 *  %NL80211_TID_CONFIG_ATTR_RETRY_LONG attributes
 * @sar_capa: SAR control capabilities
 * @rfkill: a pointer to the rfkill structure
 *
 * @mbssid_max_interfaces: maximum number of interfaces supported
 *  by the driver in a multiple BSSID set. This field must be set
 *  to a non-zero value by the driver to advertise MBSSID support.
 * @ema_max_profile_periodicity: maximum profile periodicity supported by
 *  the driver. Setting this field to a non-zero value indicates that the
 *  driver supports enhanced multi-BSSID advertisements (EMA AP).
 * @max_num_akm_suites: maximum number of AKM suites allowed for
 *  association, association request and association response commands
 *  through %NL80211_CMD_CONNECT, %NL80211_CMD_ASSOCIATE and
 *  %NL80211_CMD_START_AP. Set to NL80211_MAX_NR_AKM_SUITES if not set by
 *  the driver. If set by driver minimum allowed value is
 *  NL80211_MAX_NR_AKM_SUITES in order to avoid compatibility issues with
 *  legacy userspace and maximum allowed value is
 *  CFG80211_MAX_NUM_AKM_SUITES.
 */

struct wiphy
{
  mutex_t mtx;

  /* assign these fields before you register the wiphy */

  uint8_t perm_addr[ETH_ALEN];
  uint8_t addr_mask[ETH_ALEN];

  struct mac_address *addresses;
  const struct ieee80211_txrx_stypes *mgmt_stypes;
  const struct ieee80211_iface_combination *iface_combinations;
  int n_iface_combinations;
  uint16_t software_iftypes;
  uint16_t n_addresses;

  /* Supported interface modes, OR together BIT(NL80211_IFTYPE_...) */

  uint16_t interface_modes;
  uint16_t max_acl_mac_addrs;
  uint32_t flags;
  uint32_t regulatory_flags;
  uint32_t features;
  uint8_t ext_features[4];
  uint32_t ap_sme_capa;
  enum cfg80211_signal_type signal_type;
  int bss_priv_size;
  uint8_t max_scan_ssids;
  uint8_t max_sched_scan_reqs;
  uint8_t max_sched_scan_ssids;
  uint8_t max_match_sets;
  uint16_t max_scan_ie_len;
  uint16_t max_sched_scan_ie_len;
  uint32_t max_sched_scan_plans;
  uint32_t max_sched_scan_plan_interval;
  uint32_t max_sched_scan_plan_iterations;
  int n_cipher_suites;
  const uint32_t *cipher_suites;
  int n_akm_suites;
  const uint32_t *akm_suites;
  unsigned int num_iftype_akm_suites;
  uint8_t retry_short;
  uint8_t retry_long;
  uint32_t frag_threshold;
  uint32_t rts_threshold;
  uint8_t coverage_class;
  char fw_version[32];
  uint32_t hw_version;
  uint16_t max_remain_on_channel_duration;
  uint8_t max_num_pmkids;
  uint32_t available_antennas_tx;
  uint32_t available_antennas_rx;
  uint32_t probe_resp_offload;
  const uint8_t *extended_capabilities;
  const uint8_t *extended_capabilities_mask;
  uint8_t extended_capabilities_len;
  const struct wiphy_iftype_ext_capab *iftype_ext_capab;
  unsigned int num_iftype_ext_capab;
  const void *privid;
  struct ieee80211_supported_band *bands[NUM_NL80211_BANDS];

  /* fields below are read-only, assigned by cfg80211 */

  const struct ieee80211_regdomain *regd;
  bool registered;
  struct dentry *debugfsdir;
  const struct ieee80211_ht_cap *ht_capa_mod_mask;
  const struct ieee80211_vht_cap *vht_capa_mod_mask;
  struct list_node wdev_list;

#ifdef CONFIG_CFG80211_WEXT
  const struct iw_handler_def *wext;
#endif

  const struct nl80211_vendor_cmd_info *vendor_events;
  int n_vendor_commands;
  int n_vendor_events;
  uint16_t max_ap_assoc_sta;
  uint8_t max_num_csa_counters;
  uint32_t bss_select_support;
  uint8_t nan_supported_bands;
  uint32_t txq_limit;
  uint32_t txq_memory_limit;
  uint32_t txq_quantum;
  unsigned long tx_queue_len;
  uint8_t support_mbssid:1;
  uint8_t support_only_he_mbssid:1;

  struct
  {
    uint64_t peer;
    uint64_t vif;
    uint8_t max_retry;
  } tid_config_support;

  uint8_t max_data_retry_count;
  const struct cfg80211_sar_capa *sar_capa;
  uint8_t mbssid_max_interfaces;
  uint8_t ema_max_profile_periodicity;
  uint16_t max_num_akm_suites;

  char priv[] __attribute__((aligned(32)));
};

/* internal structs */

struct cfg80211_conn;
struct cfg80211_internal_bss;
struct cfg80211_cached_keys;
struct cfg80211_cqm_config;

/**
 * struct cfg80211_link - Link information
 *
 * @addr: MAC address (aligned to 2 bytes)
 * @union: Union containing mode-specific data
 *   @ap: AP mode data
 *     @beacon_interval: Beacon interval
 *     @chandef: Channel definition
 *   @client: Client mode data
 *     @current_bss: Pointer to current BSS
 */

struct cfg80211_link
{
  uint8_t addr[ETH_ALEN] __attribute__((aligned(2)));
  union
  {
    struct
    {
      unsigned int beacon_interval;
      struct cfg80211_chan_def chandef;
    } ap;
    struct
    {
      struct cfg80211_internal_bss *current_bss;
    } client;
  };
};

/**
 * struct wireless_dev - wireless device state
 *
 * For netdevs, this structure must be allocated by the driver
 * that uses the ieee80211_ptr field in struct net_driver_s (this
 * is intentional so it can be allocated along with the netdev.)
 * It need not be registered then as netdev registration will
 * be intercepted by cfg80211 to see the new wireless device,
 * however, drivers must lock the wiphy before registering or
 * unregistering netdevs if they pre-create any netdevs (in ops
 * called from cfg80211, the wiphy is already locked.)
 *
 * For non-netdev uses, it must also be allocated by the driver
 * in response to the cfg80211 callbacks that require it, as
 * there's no netdev registration in that case it may not be
 * allocated outside of callback operations that return it.
 *
 * @wiphy: pointer to hardware description
 * @iftype: interface type
 * @registered: is this wdev already registered with cfg80211
 * @registering: indicates we're doing registration under wiphy lock
 *  for the notifier
 * @list: (private) Used to collect the interfaces
 * @netdev: (private) Used to reference back to the netdev, may be %NULL
 * @identifier: (private) Identifier used in nl80211 to identify this
 *  wireless device if it has no netdev
 * @u: union containing data specific to @iftype
 * @connected: indicates if connected or not (STA mode)
 * @bssid: (private) Used by the internal configuration code
 * @wext: (private) Used by the internal wireless extensions compat code
 * @wext.ibss: (private) IBSS data part of wext handling
 * @wext.connect: (private) connection handling data
 * @wext.keys: (private) (WEP) key data
 * @wext.ie: (private) extra elements for association
 * @wext.ie_len: (private) length of extra elements
 * @wext.bssid: (private) selected network BSSID
 * @wext.ssid: (private) selected network SSID
 * @wext.default_key: (private) selected default key index
 * @wext.default_mgmt_key: (private) selected default management key index
 * @wext.prev_bssid: (private) previous BSSID for reassociation
 * @wext.prev_bssid_valid: (private) previous BSSID validity
 * @use_4addr: indicates 4addr mode is used on this interface, must be
 *  set by driver (if supported) on add_interface BEFORE registering the
 *  netdev and may otherwise be used by driver read-only, will be update
 *  by cfg80211 on change_interface
 * @mgmt_registrations: list of registrations for management frames
 * @mgmt_registrations_need_update: mgmt registrations were updated,
 *  need to propagate the update to the driver
 * @mtx: mutex used to lock data in this struct, may be used by drivers
 *  and some API functions require it held
 * @beacon_interval: beacon interval used on this device for transmitting
 *  beacons, 0 when not valid
 * @address: The address for this device, valid only if @netdev is %NULL
 * @is_running: true if this is a non-netdev device that has been
 *  started, e.g. the P2P Device.
 * @cac_started: true if DFS channel availability check has been started
 * @cac_start_time: timestamp (jiffies) when the dfs state was entered.
 * @cac_time_ms: CAC time in ms
 * @ps: powersave mode is enabled
 * @ps_timeout: dynamic powersave timeout
 * @ap_unexpected_nlportid: (private) netlink port ID of application
 *  registered for unexpected class 3 frames (AP mode)
 * @conn: (private) cfg80211 software SME connection state machine data
 * @connect_keys: (private) keys to set after connection is established
 * @conn_bss_type: connecting/connected BSS type
 * @conn_owner_nlportid: (private) connection owner socket port ID
 * @disconnect_wk: (private) auto-disconnect work
 * @disconnect_bssid: (private) the BSSID to use for auto-disconnect
 * @event_list: (private) list for internal event processing
 * @event_lock: (private) lock for event list
 * @owner_nlportid: (private) owner socket port ID
 * @nl_owner_dead: (private) owner socket went away
 * @cqm_config: (private) nl80211 RSSI monitor state
 * @pmsr_list: (private) peer measurement requests
 * @pmsr_lock: (private) peer measurements requests/results lock
 * @pmsr_free_wk: (private) peer measurements cleanup work
 * @unprot_beacon_reported: (private) timestamp of last
 *  unprotected beacon report
 * @links: (private) array of %IEEE80211_MLD_MAX_NUM_LINKS
 *  elements containing @addr, @ap and @client for each link
 * @valid_links: (private) bitmap describing what elements of
 *  @links are valid
 */

struct wireless_dev
{
  struct net_driver_s *netdev;
  struct wiphy *wiphy;
  enum nl80211_iftype iftype;

  /* The remainder of this struct should be private to cfg80211 */

  struct list_node list;
  uint32_t identifier;
  struct list_node mgmt_registrations;
  uint8_t mgmt_registrations_need_update:1;
  mutex_t mtx;

  bool use_4addr;
  bool is_running;
  bool registered;
  bool registering;

  uint8_t address[ETH_ALEN] __attribute__((aligned(sizeof(uint16_t))));

  /* Currently used for IBSS and SME - might be rearranged later */

  struct cfg80211_conn *conn;
  struct cfg80211_cached_keys *connect_keys;
  uint32_t conn_owner_nlportid;

  struct work_s disconnect_wk;
  uint8_t disconnect_bssid[ETH_ALEN];

  struct list_node event_list;
  mutex_t event_lock;

  uint8_t connected:1;

  bool ps;
  int ps_timeout;

  uint32_t ap_unexpected_nlportid;

  uint32_t owner_nlportid;
  bool nl_owner_dead;

  /* FIXME: need to rework radar detection for MLO */

  bool cac_started;
  unsigned long cac_start_time;
  unsigned int cac_time_ms;

#ifdef CONFIG_CFG80211_WEXT

  /* WEXT data */

  struct
  {
    struct cfg80211_ibss_params ibss;
    struct cfg80211_connect_params connect;
    struct cfg80211_cached_keys *keys;
    const uint8_t *ie;
    size_t ie_len;
    uint8_t bssid[ETH_ALEN];
    uint8_t prev_bssid[ETH_ALEN];
    uint8_t ssid[IEEE80211_MAX_SSID_LEN];
    int8_t default_key;
    int8_t default_mgmt_key;
    bool prev_bssid_valid;
  } wext;
#endif

  struct cfg80211_cqm_config *cqm_config;
  struct list_node pmsr_list;
  mutex_t pmsr_lock;
  struct work_s pmsr_free_wk;
  unsigned long unprot_beacon_reported;

  union
  {
    struct
    {
      uint8_t connected_addr[ETH_ALEN];
      uint8_t ssid[IEEE80211_MAX_SSID_LEN];
      uint8_t ssid_len;
    } client;
    struct
    {
      int beacon_interval;
      struct cfg80211_chan_def preset_chandef;
      struct cfg80211_chan_def chandef;
      uint8_t id[IEEE80211_MAX_SSID_LEN];
      uint8_t id_len;
      uint8_t id_up_len;
    } mesh;
    struct
    {
      struct cfg80211_chan_def preset_chandef;
      uint8_t ssid[IEEE80211_MAX_SSID_LEN];
      uint8_t ssid_len;
    } ap;
    struct
    {
      struct cfg80211_internal_bss *current_bss;
      struct cfg80211_chan_def chandef;
      int beacon_interval;
      uint8_t ssid[IEEE80211_MAX_SSID_LEN];
      uint8_t ssid_len;
    } ibss;
    struct
    {
      struct cfg80211_chan_def chandef;
    } ocb;
  } u;

  struct cfg80211_link links[IEEE80211_MLD_MAX_NUM_LINKS];
  uint16_t valid_links;
  void *priv;
};

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wiphy_priv
 *
 * Description:
 *   Return pointer to the driver private data area in wiphy.
 *
 ****************************************************************************/

static inline FAR void *wiphy_priv(FAR struct wiphy *wiphy)
{
  return (FAR void *)wiphy->priv;
}

/****************************************************************************
 * Name: wiphy_new
 *
 * Description:
 *   Allocate a new wiphy with space for driver private data.
 *
 ****************************************************************************/

static inline FAR struct wiphy *
wiphy_new(FAR const struct cfg80211_ops *ops, int sizeof_priv)
{
  FAR struct wiphy *wiphy;

  (void)ops;
  wiphy = (FAR struct wiphy *)kmm_memalign(32,
                                           sizeof(*wiphy) + sizeof_priv);
  if (wiphy == NULL)
    {
      return NULL;
    }

  memset(wiphy, 0, sizeof(*wiphy) + sizeof_priv);

  nxmutex_init(&wiphy->mtx);
  list_initialize(&wiphy->wdev_list);
  return wiphy;
}

/****************************************************************************
 * Name: wiphy_free
 *
 * Description:
 *   Free a wiphy previously allocated with wiphy_new.
 *
 ****************************************************************************/

static inline void wiphy_free(FAR struct wiphy *wiphy)
{
  if (wiphy != NULL)
    {
      nxmutex_destroy(&wiphy->mtx);
      kmm_free(wiphy);
    }
}

/****************************************************************************
 * Name: wiphy_register
 *
 * Description:
 *   Register a wiphy with the cfg80211 framework.
 *   Stub implementation - marks wiphy as registered.
 *
 ****************************************************************************/

static inline int wiphy_register(FAR struct wiphy *wiphy)
{
  wiphy->registered = true;
  return 0;
}

/****************************************************************************
 * Name: wiphy_unregister
 *
 * Description:
 *   Unregister a wiphy from the cfg80211 framework.
 *   Stub implementation - marks wiphy as unregistered.
 *
 ****************************************************************************/

static inline void wiphy_unregister(FAR struct wiphy *wiphy)
{
  wiphy->registered = false;
}

#endif /* __INCLUDE_NET_CFG80211_H */
