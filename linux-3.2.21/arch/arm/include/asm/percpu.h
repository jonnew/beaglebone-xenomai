#ifndef __ARM_PERCPU
#define __ARM_PERCPU

#if defined(CONFIG_IPIPE) && defined(CONFIG_SMP)
#define __my_cpu_offset	per_cpu_offset(cpu_logical_map(hard_smp_processor_id()))
#endif

#include <asm-generic/percpu.h>

#endif
