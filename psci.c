#include <stdint.h>
#include "xprintf.h"
#include "system.h"
#include "stm32mp1.h"

#define CBAR_MASK                       0xFFFF8000
#define GIC_DIST_OFFSET         0x1000
#define GICD_SGIR               0x0f00
#define GICD_CTLR               0x0000
#define GICD_TYPER              0x0004
#define GICD_IGROUPRn           0x0080

static uint32_t stm32mp_get_gicd_base_address()
{
	uint32_t periphbase;

	/* get the GIC base address from the CBAR register */
	asm("mrc p15, 4, %0, c15, c0, 0\n" : "=r" (periphbase));

	return (periphbase & CBAR_MASK) + GIC_DIST_OFFSET;
}

void gicc_disable()
{
	volatile uint32_t *ig = (void*)stm32mp_get_gicd_base_address();
	ig[0x400] = 0; // GICC_CTLR
}

void stm32mp_init_gic(int cpu2)
{
	// need to do in secure state
	uint32_t gic_dist_addr;
	gic_dist_addr = stm32mp_get_gicd_base_address();
	volatile uint32_t *ig = (void*)(gic_dist_addr + GICD_IGROUPRn);
	if (!cpu2) {
		volatile uint32_t *ctl = (void*)(gic_dist_addr + GICD_CTLR);
		*ctl |= 3; // enable distributor
		volatile uint32_t *type = (void*)(gic_dist_addr + GICD_TYPER);
		int irq_cnt = *type & 0x1f,i;
		// set all irqs as nonsecure
		for (i = 0; i <= irq_cnt; i++) ig[i] = 0xffffffff;
	} else {
		// only local IRQs for second CPU
		ig[0] = 0xffffffff;
	}
	volatile uint32_t *cpui = (void*)(gic_dist_addr + 0x1000);
	cpui[1] = 0xff; // low prio to allow NS to override it
}

static void stm32mp_smp_kick_all_cpus()
{
	uint32_t gic_dist_addr;

	gic_dist_addr = stm32mp_get_gicd_base_address();

	/* kick all CPUs (except this one) by writing to GICD_SGIR */
	volatile uint32_t *p = (void*)(gic_dist_addr + GICD_SGIR);
	*p = 1U << 24;
}

void start_cpu2(void *addr)
{
	TAMP->BKP5R = (uint32_t)addr;
	TAMP->BKP4R = BOOT_API_A7_CORE1_MAGIC_NUMBER;
	stm32mp_smp_kick_all_cpus();
}

static void (*sec_boota)();
void boot_secondary();
void setup_ns_mode();
void cpu2_entry()
{
	stm32mp_init_gic(1);

	// init timer freq register, we assume CPU0 is
	// already done with it
	volatile uint32_t *STG = (void*)STGENC_BASE;
	asm ("mcr p15, 0, %0, c14, c0, 0"::"r"(STG[8]));

	call_smc(SMC_NSEC,0,0); // switch to NS state
	setup_ns_mode();
	sec_boota();
}

uint32_t psci_handler(uint32_t cmd,uint32_t a1,uint32_t a2,uint32_t a3)
{
	if (cmd == 0x84000000) {
		return 0x2;
	}
	if (cmd == 0x84000003) {
		//return -1;
		xprintf("start cpu %X %X %X\n",a1,a2,a3);
		sec_boota = (void*)a2;
		if (a1 != 1) return -2;
		start_cpu2(&boot_secondary);
		return 0;
	}
	if (cmd == 0x84000008) { // off
		// XXX if (mach->off) mach->off();
		return -1;
	}
	if (cmd == 0x84000009) { // reset
		// XXX if (mach->reset) mach->reset();
		RCC->MP_GRSTCSETR = RCC_MP_GRSTCSETR_MPSYSRST;
		for (;;) __WFI();
	}
	return -1;
}
