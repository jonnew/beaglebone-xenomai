/*
 * arch/arm/plat-omap/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for OMAP-based platforms
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/irqs.h>
#include <asm/hardware/gic.h>

#include <plat/omap24xx.h>
#include <plat/omap34xx.h>
#include <plat/omap44xx.h>

#include <plat/multi.h>

#define OMAP2_IRQ_BASE          OMAP2_L4_IO_ADDRESS(OMAP24XX_IC_BASE)
#define OMAP3_IRQ_BASE          OMAP2_L4_IO_ADDRESS(OMAP34XX_IC_BASE)
#define OMAP4_IRQ_BASE          OMAP2_L4_IO_ADDRESS(OMAP44XX_GIC_CPU_BASE)
#define INTCPS_SIR_IRQ_OFFSET   0x0040  /* omap2/3 active interrupt offset */
#define ACTIVEIRQ_MASK          0x7f    /* omap2/3 active interrupt bits */

		.macro	disable_fiq
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm
		
/*
 * Optimized irq functions for omap2, 3 and 4
 */

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
                .macro  get_irqnr_preamble, base, tmp
#ifdef CONFIG_ARCH_OMAP2
                ldr     \base, =OMAP2_IRQ_BASE
#else
                ldr     \base, =OMAP3_IRQ_BASE
#endif
                .endm

                /* Check the pending interrupts. Note that base already set */
                .macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
                ldr     \irqnr, [\base, #0x98] /* IRQ pending reg 1 */
                cmp     \irqnr, #0x0
                bne     9999f
                ldr     \irqnr, [\base, #0xb8] /* IRQ pending reg 2 */
                cmp     \irqnr, #0x0
                bne     9999f
                ldr     \irqnr, [\base, #0xd8] /* IRQ pending reg 3 */
                cmp     \irqnr, #0x0
9999:
                ldrne   \irqnr, [\base, #INTCPS_SIR_IRQ_OFFSET]
                and     \irqnr, \irqnr, #ACTIVEIRQ_MASK /* Clear spurious bits */

                .endm
#endif
