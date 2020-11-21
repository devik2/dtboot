#include <stdint.h>
#include "xprintf.h"
#include "system.h"
#include "stm32mp157axx_cm4.h"

#define BOOT_API_A7_CORE0_MAGIC_NUMBER  0xCA7FACE0
#define BOOT_API_A7_CORE1_MAGIC_NUMBER  0xCA7FACE1
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
		ig[0] = 0xffffffff;
	}
	volatile uint32_t *cpui = (void*)(gic_dist_addr + 0x1000);
	cpui[1] = 0xff; // low prio to allow NS to override it
	//asm("bkpt");
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

extern uint32_t sec_boota;
void boot_secondary();
volatile uint32_t cpu2_tgt;
void park_cpu2()
{
	cpu2_tgt = 7;
	while (cpu2_tgt<10);
	void (*fn)();
	fn = (void*)cpu2_tgt;
//	asm("bkpt");
	fn();
}

void prep_cpu2()
{
	xprintf("preparing CPU2\n");
	start_cpu2(&boot_secondary);
	udelay(1000);
	xprintf("CPU2 status: %d\n",cpu2_tgt);
}

int boot_sec()
{
	return -1;
	if (!cpu2_tgt) return -1; // unprepared CPU2
	cpu2_tgt = 0xc0102240;
	/*
	uint32_t *pgm = (void*)0xc0004000;
	pgm[0x5c0] = 0x5c011402;
	pgm[0xa00] = 0xa0011402;
	sec_boota = 0xc0102240;
	*/
	return 0;
}

uint32_t psci_handler(uint32_t cmd,uint32_t a1,uint32_t a2,uint32_t a3)
{
	if (cmd == 0x84000000) {
		return 0x2;
	}
	if (cmd == 0x84000003) {
		return -1;
		xprintf("start cpu %X %X %X\n",a1,a2,a3);
		sec_boota = a2;
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
