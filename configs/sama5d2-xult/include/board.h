/************************************************************************************
 * configs/sama5d2-xult/include/board.h
 *
 *   Copyright (C) 2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ************************************************************************************/

#ifndef __CONFIGS_SAMA5D2_XULT_INCLUDE_BOARD_H
#define __CONFIGS_SAMA5D2_XULT_INCLUDE_BOARD_H

/************************************************************************************
 * Included Files
 ************************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <nuttx/irq.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

/* Clocking *************************************************************************/
/* After power-on reset, the SAMA5 device is running on a 12MHz internal RC.  These
 * definitions will configure operational clocking.
 */

/* On-board crystal frequencies */

#define BOARD_MAINOSC_FREQUENCY    (12000000)  /* MAINOSC: 12MHz crystal on-board */
#define BOARD_SLOWCLK_FREQUENCY    (32768)     /* Slow Clock: 32.768KHz */

#if defined(CONFIG_SAMA5_BOOT_SDRAM)
/* When booting from SDRAM, NuttX is loaded in SDRAM by an intermediate bootloader.
 * That bootloader had to have already configured the PLL and SDRAM for proper
 * operation.
 *
 * In this case, we don not reconfigure the clocking.  Rather, we need to query
 * the register settings to determine the clock frequencies.  We can only assume that
 * the Main clock source is the on-board 12MHz crystal.
 */

#  include <arch/board/board_sdram.h>

#elif defined(CONFIG_SAMA5D2XULT_384MHZ)
/* OHCI Only.  This is an alternative slower configuration that will produce a 48MHz
 * USB clock with the required accuracy using only PLLA.  When PPLA is used to clock
 * OHCI, an additional requirement is the PLLACK be a multiple of 48MHz.  This setup
 * results in a CPU clock of 384MHz.
 *
 * This case is only interesting for experimentation.
 */

#  include <arch/board/board_384mhz.h>

#elif defined(CONFIG_SAMA5D2XULT_528MHZ)
/* This is the configuration results in a CPU clock of 528MHz.
 *
 * In this configuration, UPLL is the source of the UHPHS clock (if enabled).
 */

#  include <arch/board/board_528mhz.h>

#else /* #elif defined(CONFIG_SAMA5D2XULT_396MHZ) */
/* This is the configuration provided in the Atmel example code.  This setup results
 * in a CPU clock of 396MHz.
 *
 * In this configuration, UPLL is the source of the UHPHS clock (if enabled).
 */

#  include <arch/board/board_396mhz.h>

#endif

/* LED definitions ******************************************************************/
/* There is an RGB LED on board the SAMA5D2-XULT.  The RED component is driven by
 * the SDHC_CD pin (PA13) and so will not be used.  The LEDs are provided VDD_LED
 * and so bringing the LED low will will illuminated the LED.
 *
 * ------------------------------ ------------------- -------------------------
 * SAMA5D2 PIO                    SIGNAL              USAGE
 * ------------------------------ ------------------- -------------------------
 * PA13                           SDHC_CD_PA13        Red LED
 * PB5                            LED_GREEN_PB5       Green LED
 * PB0                            LED_BLUE_PB0        Blue LED
 * ------------------------------ ------------------- -------------------------
 */

#ifndef CONFIG_ARCH_LEDS
/* LED index values for use with sam_setled() */

#define BOARD_GREEN       0
#define BOARD_BLUE        1
#define BOARD_NLEDS       2

/* LED bits for use with sam_setleds() */

#define BOARD_GREEN_BIT  (1 << BOARD_GREEN)
#define BOARD_BLUE_BIT   (1 << BOARD_BLUE)

#else
/* LED index values for use with sam_setled() */

#define BOARD_BLUE        0
#define BOARD_NLEDS       1

/* LED bits for use with sam_setleds() */

#define BOARD_BLUE_BIT   (1 << BOARD_BLUE)
#endif

/* These LEDs are not used by the board port unless CONFIG_ARCH_LEDS is
 * defined.  In that case, the usage by the board port is defined in
 * include/board.h and src/sam_leds.c. The LEDs are used to encode OS-related
 * events as follows.  Note that only the GREEN LED is used in this case
 *
 *      SYMBOL            Val    Meaning                   Green LED
 *      ----------------- ---   -----------------------  ----------- */
#define LED_STARTED       0  /* NuttX has been started     OFF       */
#define LED_HEAPALLOCATE  0  /* Heap has been allocated    OFF       */
#define LED_IRQSENABLED   0  /* Interrupts enabled         OFF       */
#define LED_STACKCREATED  1  /* Idle stack created         ON        */
#define LED_INIRQ         2  /* In an interrupt            N/C       */
#define LED_SIGNAL        2  /* In a signal handler        N/C       */
#define LED_ASSERTION     2  /* An assertion failed        N/C       */
#define LED_PANIC         3  /* The system has crashed     Flash     */
#undef  LED_IDLE             /* MCU is is sleep mode       Not used  */

/* Thus if the Green LED is statically on, NuttX has successfully  booted and
 * is, apparently, running normally.  If LED is flashing at approximately
 * 2Hz, then a fatal error has been detected and the system has halted.
 */

/* Button definitions ***************************************************************/
/* A single button, PB_USER (PB6), is available on the SAMA5D2-XULT
 *
 *  ------------------------------ ------------------- -------------------------
 *  SAMA5D2 PIO                    SIGNAL              USAGE
 *  ------------------------------ ------------------- -------------------------
 *  PB6                            USER_PB_PB6         PB_USER push button
 *  ------------------------------ ------------------- -------------------------
 *
 *  Closing PB_USER will bring PB6 to ground so 1) PB6 should have a weak pull-up,
 * and 2) when PB_USER is pressed, a low value will be senses.
 */

#define BUTTON_USER       0
#define NUM_BUTTONS       1

#define BUTTON_USER_BIT   (1 << BUTTON_USER)

/* Pin disambiguation ***************************************************************/
/* Alternative pin selections are provided with a numeric suffix like _1, _2, etc. 
 * Drivers, however, will use the pin selection without the numeric suffix. 
 * Additional definitions are required in this board.h file.  For example, if we
 * wanted the PCK0on PB26, then the following definition should appear in the
 * board.h header file for that board:
 *
 *   #define PIO_PMC_PCK0 PIO_PMC_PCK0_1
 *
 * The PCK logic will then automatically configure PB26 as the PCK0 pin.
 */

/************************************************************************************
 * Assembly Language Macros
 ************************************************************************************/

#ifdef __ASSEMBLY__
	.macro	config_sdram
	.endm
#endif /* __ASSEMBLY__ */

/************************************************************************************
 * Public Data
 ************************************************************************************/

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C" {
#else
#define EXTERN extern
#endif

/************************************************************************************
 * Public Function Prototypes
 ************************************************************************************/
/************************************************************************************
 * Name: sam_boardinitialize
 *
 * Description:
 *   All SAMA5 architectures must provide the following entry point.  This entry point
 *   is called early in the initialization -- after all memory has been configured
 *   and mapped but before any devices have been initialized.
 *
 ************************************************************************************/

void sam_boardinitialize(void);

/************************************************************************************
 * Name:  sam_ledinit, sam_setled, and sam_setleds
 *
 * Description:
 *   The following interfaces are available to control the LEDs from user
 *   applications.  If CONFIG_ARCH_LEDS is defined, then NuttX will control the
 *   green LEDs.
 *
 ************************************************************************************/

void sam_ledinit(void);
void sam_setled(int led, bool ledon);
void sam_setleds(uint8_t ledset);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* !__ASSEMBLY__ */
#endif  /* __CONFIGS_SAMA5D2_XULT_INCLUDE_BOARD_H */
