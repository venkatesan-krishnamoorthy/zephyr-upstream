/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_MISC_TI_AM13_ESCSS_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_MISC_TI_AM13_ESCSS_H_

/* LATCH0/LATCH1 input source select (ESCSS LATCH_SEL) */
#define TI_ESCSS_LATCH_PIN_LATCH0   0
#define TI_ESCSS_LATCH_PIN_LATCH1   1
#define TI_ESCSS_LATCH_CPU_NMI      2
/* Input XBAR output n, n = 1..16 */
#define TI_ESCSS_LATCH_INPUTXBAR(n) (2 + (n))
/* PWM XBAR output n, n = 1..8 */
#define TI_ESCSS_LATCH_PWMXBAR(n)   (18 + (n))

/* EtherCAT ResetOut destinations (ESCSS RESET_DEST_CONFIG) */
#define TI_ESCSS_RESET_DEST_CPU_RESET    0x1
#define TI_ESCSS_RESET_DEST_CPU_NMI      0x2
#define TI_ESCSS_RESET_DEST_CPU_INT      0x4
#define TI_ESCSS_RESET_DEST_DEVICE_RESET 0x80

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_MISC_TI_AM13_ESCSS_H_ */
