#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clocksource.h>
#include <linux/sched.h>
#include <linux/ipipe_tickdev.h>

#include <linux/ipipe.h>

#include <asm/cacheflush.h>
#include <asm/traps.h>

typedef unsigned long long __ipipe_tsc_t(void);

extern __ipipe_tsc_t __ipipe_freerunning_64,
	__ipipe_freerunning_32,
	__ipipe_freerunning_16,
	__ipipe_freerunning_countdown,
	__ipipe_decrementer_16,
	__ipipe_freerunning_twice_16;
extern unsigned long __ipipe_tsc_addr;

static struct __ipipe_tscinfo tsc_info;

static struct clocksource clksrc = {
	.name = "ipipe_tsc",
	.rating = 0x7fffffff,
	.read = (typeof(clksrc.read))__ipipe_tsc_get,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

struct ipipe_tsc_value_t {
	unsigned long long last_tsc;
	unsigned last_cnt;
};

unsigned long __ipipe_kuser_tsc_freq;

struct ipipe_tsc_value_t *ipipe_tsc_value;

void __ipipe_tsc_register(struct __ipipe_tscinfo *info)
{
	unsigned long *tsc_addr;
	__ipipe_tsc_t *implem;
	unsigned long flags;
	char *tsc_area;

#if !defined(CONFIG_CPU_USE_DOMAINS)
	extern char __ipipe_tsc_area_start[], __kuser_helper_end[];

	tsc_area = (char *)vectors_page + 0x1000
		+ (__ipipe_tsc_area_start - __kuser_helper_end);
	tsc_addr = (unsigned long *)
		(tsc_area + ((char *)&__ipipe_tsc_addr - __ipipe_tsc_area));
#else
	tsc_area = __ipipe_tsc_area;
	tsc_addr = &__ipipe_tsc_addr;
#endif
	ipipe_tsc_value = (struct ipipe_tsc_value_t *)tsc_area;

	switch(info->type) {
	case IPIPE_TSC_TYPE_FREERUNNING:
		switch(info->u.mask) {
		case 0xffff:
			implem = &__ipipe_freerunning_16;
			break;
		case 0xffffffff:
			implem = &__ipipe_freerunning_32;
			break;
		case 0xffffffffffffffffULL:
			implem = &__ipipe_freerunning_64;
			break;
		default:
			goto unimplemented;
		}
		break;

	case IPIPE_TSC_TYPE_DECREMENTER:
		if (info->u.mask != 0xffff)
			goto unimplemented;
		implem = &__ipipe_decrementer_16;
		break;

	case IPIPE_TSC_TYPE_FREERUNNING_COUNTDOWN:
		if (info->u.mask != 0xffffffff)
			goto unimplemented;
		implem = &__ipipe_freerunning_countdown;
		break;

	case IPIPE_TSC_TYPE_FREERUNNING_TWICE:
		if (info->u.mask != 0xffff)
			goto unimplemented;
		implem = &__ipipe_freerunning_twice_16;
		break;

	default:
	unimplemented:
		printk("I-pipel: Unimplemented tsc configuration, "
		       "type: %d, mask: 0x%08Lx\n", info->type, info->u.mask);
		BUG();
	}

	tsc_info = *info;
	*tsc_addr = tsc_info.counter_vaddr;
	if (tsc_info.type == IPIPE_TSC_TYPE_DECREMENTER) {
		tsc_info.u.dec.last_cnt = &ipipe_tsc_value->last_cnt;
		tsc_info.u.dec.tsc = &ipipe_tsc_value->last_tsc;
	} else
		tsc_info.u.fr.tsc = &ipipe_tsc_value->last_tsc;

	flags = hard_local_irq_save();
	memcpy(tsc_area + 0x20, implem, 0x60);
	flush_icache_range((unsigned long)(tsc_area),
			   (unsigned long)(tsc_area + 0x80));
	hard_local_irq_restore(flags);

	clksrc.shift = fls(tsc_info.freq) - 1;
	clksrc.mult = clocksource_hz2mult(tsc_info.freq, clksrc.shift);
	printk(KERN_INFO "I-pipe, %u.%03u MHz clocksource\n",
	       tsc_info.freq / 1000000, (tsc_info.freq % 1000000) / 1000);
	clocksource_register(&clksrc);

	__ipipe_kuser_tsc_freq = tsc_info.freq;
}

void __ipipe_mach_get_tscinfo(struct __ipipe_tscinfo *info)
{
	*info = tsc_info;
}

void __ipipe_tsc_update(void)
{
	if (ipipe_tsc_value == NULL)
		return;

	if (tsc_info.type == IPIPE_TSC_TYPE_DECREMENTER) {
		unsigned cnt = *(unsigned *)tsc_info.counter_vaddr;
		int offset = ipipe_tsc_value->last_cnt - cnt;
		if (offset < 0)
			offset += tsc_info.u.dec.mask + 1;
		ipipe_tsc_value->last_tsc += offset;
		ipipe_tsc_value->last_cnt = cnt;
		return;
	}

	/* Update last_tsc, in order to remain compatible with legacy
	   user-space 32 bits free-running counter implementation */
	ipipe_tsc_value->last_tsc = __ipipe_tsc_get() - 1;
}
EXPORT_SYMBOL(__ipipe_tsc_get);

void update_vsyscall(struct timespec *wall_time, struct timespec *wtm,
		     struct clocksource *clock, u32 mult)
{
	if (clock == &clksrc)
		ipipe_update_hostrt(wall_time, wtm, clock, mult);
}

void update_vsyscall_tz(void)
{
}
