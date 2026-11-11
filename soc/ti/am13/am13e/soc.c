/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <soc.h>
#include <zephyr/arch/common/sys_io.h>
#include <zephyr/init.h>
#include <zephyr/linker/sections.h>
#include <zephyr/sys/barrier.h>
#include <soc_sysctl.h>

#ifdef CONFIG_AM13E_FLASH_CACHE
/*
 * Flash read interface (FRI) base is 0x40028000 (am13e.dtsi); FRD_INTF_CTRL
 * is at offset 0x100C. Bit 2 (CACHE_EN) enables the cache of the MAIN region.
 * Bit 1 (DLB_EN) only affects the NONMAIN/ENGR regions and is left alone.
 */
#define AM13E_FRI_BASE          0x40028000UL
#define AM13E_FRI_FRD_INTF_CTRL (AM13E_FRI_BASE + 0x100C)
#define AM13E_FRI_FRD_CACHE_EN  BIT(2)

/*
 * No flash access may be in flight while the FRI registers change, including
 * instructions in the pipeline (TRM 13.5.6), so run from RAM and flush the
 * pipeline afterwards.
 */
static __ramfunc int am13e_flash_cache_enable(void)
{
	sys_write32(AM13E_FRI_FRD_CACHE_EN, AM13E_FRI_FRD_INTF_CTRL);
	barrier_dsync_fence_full();
	barrier_isync_fence_full();

	return 0;
}

SYS_INIT(am13e_flash_cache_enable, PRE_KERNEL_1, 0);
#endif /* CONFIG_AM13E_FLASH_CACHE */

#ifdef CONFIG_AM13E_SRAM_UNIFIED
/*
 * AM13E SYSCTL soclock base is 0x400AF000 (am13e.dtsi).
 * AM13SPARESOCLOCK2 is at offset 0x14A4 (absolute 0x400B04A4).
 * Bit 0 (SRAM3_STATIC_MUX_SEL): 1 = C-AHB (reset default), 0 = S-AHB.
 * Writing 0 makes SRAM3 contiguous with banks 0-2 via S-AHB at 0x20018000,
 * giving one flat 128 KB region from 0x20000000.
 * R/W field, runtime effective, no SYSRSn needed (TRM SPRUJF2A Table 3-113).
 */
#define AM13E_SYSCTL_BASE    0x400AF000UL
#define AM13E_SRAM3_MUX_ADDR (AM13E_SYSCTL_BASE + SYSCTL_SRAM3_MUX_OFFSET)

void soc_reset_hook(void)
{
	sys_write32(SYSCTL_SRAM3_MUX_SEL_SBUS, AM13E_SRAM3_MUX_ADDR);
}
#endif /* CONFIG_AM13E_SRAM_UNIFIED */

/* The BL/BX instructions themselves will be worth 1-3 overhead cycles depending on optimisation */
void __noinline msp_delay_peripheral_startup(void)
{
#define NOP(i, ...)    "nop;"
#define NOP_N_TIMES(n) LISTIFY(n, NOP, ())

	__asm__ volatile(NOP_N_TIMES(CONFIG_MSPM0_PERIPH_STARTUP_DELAY));
}
