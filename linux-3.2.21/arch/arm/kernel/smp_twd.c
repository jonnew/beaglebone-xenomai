/*
 *  linux/arch/arm/kernel/smp_twd.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/jiffies.h>
#include <linux/clockchips.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/ipipe.h>
#include <linux/export.h>
#include <linux/ipipe_tickdev.h>

#include <asm/smp_twd.h>
#include <asm/localtimer.h>
#include <asm/hardware/gic.h>

/* set up by the platform code */
void __iomem *twd_base;
static struct clk *twd_clk;

static unsigned long twd_timer_rate;

static struct clock_event_device __percpu **twd_evt;
#if defined(CONFIG_IPIPE) && defined(CONFIG_SMP)
static DEFINE_PER_CPU(struct ipipe_timer, twd_itimer);

void __iomem *gt_base;

static void twd_ack(void)
{
	writel_relaxed(1, twd_base + TWD_TIMER_INTSTAT);
}

static struct __ipipe_tscinfo tsc_info;

void __cpuinit gt_setup(unsigned long base_paddr, unsigned bits)
{
	if (!gt_base) {
		gt_base = ioremap(base_paddr, SZ_256);
		BUG_ON(!gt_base);

		/* Start global timer */
		__raw_writel(1, gt_base + 0x8);

		tsc_info.type = IPIPE_TSC_TYPE_FREERUNNING;
		tsc_info.freq = twd_timer_rate;
		tsc_info.counter_vaddr = (unsigned long)gt_base;
		tsc_info.u.counter_paddr = base_paddr;

		switch(bits) {
		case 64:
			tsc_info.u.mask = 0xffffffffffffffffULL;
			break;
		case 32:
			tsc_info.u.mask = 0xffffffff;
			break;
		default:
			/* Only supported as a 32 bits or 64 bits */
			BUG();
		}

		__ipipe_tsc_register(&tsc_info);

	}
}

#ifdef CONFIG_IPIPE_DEBUG_INTERNAL

static DEFINE_PER_CPU(int, irqs);

void twd_hrtimer_debug(unsigned int irq) /* hw interrupt off */
{
	int cpu = ipipe_processor_id();

	if ((++per_cpu(irqs, cpu) % HZ) == 0) {
#if 0
		__ipipe_serial_debug("%c", 'A' + cpu);
#else
		do { } while (0);
#endif
	}
}
#endif /* CONFIG_IPIPE_DEBUG_INTERNAL */
#endif /* CONFIG_IPIPE && CONFIG_SMP */

/*
 * local_timer_ack: checks for a local timer interrupt.
 *
 * If a local timer interrupt has occurred, acknowledge and return 1.
 * Otherwise, return 0.
 */
int twd_timer_ack(void)
{
	if (__raw_readl(twd_base + TWD_TIMER_INTSTAT)) {
		__raw_writel(1, twd_base + TWD_TIMER_INTSTAT);
		return 1;
	}

	return 0;
}

static void twd_set_mode(enum clock_event_mode mode,
			struct clock_event_device *clk)
{
	unsigned long ctrl;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		ctrl = TWD_TIMER_CONTROL_ENABLE | TWD_TIMER_CONTROL_IT_ENABLE
			| TWD_TIMER_CONTROL_PERIODIC;
		__raw_writel((twd_timer_rate + HZ / 2) / HZ, twd_base + TWD_TIMER_LOAD);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* period set, and timer enabled in 'next_event' hook */
		ctrl = TWD_TIMER_CONTROL_IT_ENABLE | TWD_TIMER_CONTROL_ONESHOT;
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		ctrl = 0;
	}

	__raw_writel(ctrl, twd_base + TWD_TIMER_CONTROL);
}

static int twd_set_next_event(unsigned long evt,
			struct clock_event_device *unused)
{
	unsigned long ctrl = __raw_readl(twd_base + TWD_TIMER_CONTROL);

	ctrl |= TWD_TIMER_CONTROL_ENABLE;

	__raw_writel(evt, twd_base + TWD_TIMER_COUNTER);
	__raw_writel(ctrl, twd_base + TWD_TIMER_CONTROL);

	return 0;
}

void twd_timer_stop(struct clock_event_device *clk)
{
	twd_set_mode(CLOCK_EVT_MODE_UNUSED, clk);
	disable_percpu_irq(clk->irq);
}

static void __cpuinit twd_calibrate_rate(void)
{
	unsigned long count;
	u64 waitjiffies;

	/*
	 * If this is the first time round, we need to work out how fast
	 * the timer ticks
	 */
	if (twd_timer_rate == 0) {
		printk(KERN_INFO "Calibrating local timer... ");

		/* Wait for a tick to start */
		waitjiffies = get_jiffies_64() + 1;

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		/* OK, now the tick has started, let's get the timer going */
		waitjiffies += 5;

				 /* enable, no interrupt or reload */
		__raw_writel(0x1, twd_base + TWD_TIMER_CONTROL);

				 /* maximum value */
		__raw_writel(0xFFFFFFFFU, twd_base + TWD_TIMER_COUNTER);

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		count = __raw_readl(twd_base + TWD_TIMER_COUNTER);

		twd_timer_rate = (0xFFFFFFFFU - count) * (HZ / 5);

		printk("%lu.%02luMHz.\n", twd_timer_rate / 1000000,
			(twd_timer_rate / 10000) % 100);
	}
}

static irqreturn_t twd_handler(int irq, void *dev_id)
{
	struct clock_event_device *evt = *(struct clock_event_device **)dev_id;

	if (clockevent_ipipe_stolen(evt))
		goto handle;

	if (twd_timer_ack()) {
	  handle:
		__ipipe_tsc_update();
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * Setup the local clock events for a CPU.
 */
void __cpuinit twd_timer_setup(struct clock_event_device *clk)
{
	struct clock_event_device **this_cpu_clk;

	if (!twd_evt) {
		int err;

		twd_evt = alloc_percpu(struct clock_event_device *);
		if (!twd_evt) {
			pr_err("twd: can't allocate memory\n");
			return;
		}

		err = request_percpu_irq(clk->irq, twd_handler,
					 "twd", twd_evt);
		if (err) {
			pr_err("twd: can't register interrupt %d (%d)\n",
			       clk->irq, err);
			return;
		}
	}

	if (twd_clk == NULL) {
		twd_clk = clk_get(NULL, "smp_twd");
		if (IS_ERR(twd_clk))
			pr_warn("%s: no clock found\n", __func__);
		else
			clk_enable(twd_clk);
	}

	if (!IS_ERR(twd_clk)) {
		twd_timer_rate = clk_get_rate(twd_clk);
		printk(KERN_INFO "local timer: %lu.%02luMHz.\n",
		       twd_timer_rate / 1000000,
		       (twd_timer_rate / 10000) % 100);
	} else
		twd_calibrate_rate();

#if defined(CONFIG_IPIPE) && defined(CONFIG_SMP)
	printk(KERN_INFO "I-pipe, %lu.%03lu MHz timer\n",
	       twd_timer_rate / 1000000,
	       (twd_timer_rate % 1000000) / 1000);
	clk->ipipe_timer = __this_cpu_ptr(&twd_itimer);
	clk->ipipe_timer->irq = clk->irq;
	clk->ipipe_timer->ack = twd_ack;
	clk->ipipe_timer->min_delay_ticks = 0xf;
#endif

	clk->name = "local_timer";
	clk->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT |
			CLOCK_EVT_FEAT_C3STOP;
	clk->rating = 350;
	clk->set_mode = twd_set_mode;
	clk->set_next_event = twd_set_next_event;
	clk->shift = 20;
	clk->mult = div_sc(twd_timer_rate, NSEC_PER_SEC, clk->shift);
	clk->max_delta_ns = clockevent_delta2ns(0xffffffff, clk);
	clk->min_delta_ns = clockevent_delta2ns(0xf, clk);

	this_cpu_clk = __this_cpu_ptr(twd_evt);
	*this_cpu_clk = clk;

	clockevents_register_device(clk);

	enable_percpu_irq(clk->irq, 0);
}
