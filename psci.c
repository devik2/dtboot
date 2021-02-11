#include <stdint.h>
#include "xprintf.h"
#include "system.h"
#include "stm32mp1.h"
#include "fdt.h"

#define CBAR_MASK                       0xFFFF8000
#define GIC_DIST_OFFSET         0x1000
#define GICD_SGIR               0x0f00
#define GICD_CTLR               0x0000
#define GICD_TYPER              0x0004
#define GICD_IGROUPRn           0x0080

#define GICd_ISENABLER 0x40
#define GICd_SGIR (GICD_SGIR/4)
#define GICC_CTLR 0x400
#define GICC_PMR 0x401
#define GICC_IAR 0x403
#define GICC_EOIR 0x404

static uint32_t stm32mp_get_gicd_base_address()
{
	uint32_t periphbase;

	/* get the GIC base address from the CBAR register */
	asm("mrc p15, 4, %0, c15, c0, 0\n" : "=r" (periphbase));

	return (periphbase & CBAR_MASK) + GIC_DIST_OFFSET;
}

void gicc_disable()
{
	volatile uint32_t *gic = (void*)stm32mp_get_gicd_base_address();
	gic[GICC_CTLR] = 0;
}

void stm32mp_init_gic(int cpu2)
{
	// need to do in secure state
	uint32_t gic_dist_addr = stm32mp_get_gicd_base_address();
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
	volatile uint32_t *gic = (void*)gic_dist_addr;
	gic[GICC_PMR] = 0xff; // low prio to allow NS to override it
}

static void gicc_enable()
{
	volatile uint32_t *gic = (void*)stm32mp_get_gicd_base_address();
	gic[GICC_CTLR] = 3;
	gic[GICd_ISENABLER] = 0xffff; // enable SGIs
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

static void loop_delay(int n)
{
	volatile int i;
	for (i=0;i<n;i++);
}

volatile int _led,_cnt,_iar,_irqs;
void irq_handler()
{
	volatile uint32_t *gic = (void*)stm32mp_get_gicd_base_address();
	_iar = gic[GICC_IAR];
	_irqs++;
	_led = !_led;
	GPIOH->BSRR = _led ? (1<<5) : (0x10000<<5);
	gic[GICC_EOIR] = _iar;
}

static void secondary_test()
{
	__disable_irq();
	for(;;) {
		__WFI();
		_cnt++;
	}
}

#define CPUST_ON 0
#define CPUST_OFF 1
#define CPUST_PEND 2
volatile static uint8_t sec_cpu_state = CPUST_OFF;
static uint8_t can_boot_sec;
static void (*sec_boota)();
void boot_secondary();
void setup_ns_mode();
void cpu2_entry() // called from boot_secondary()
{
	stm32mp_init_gic(1);

	// init timer freq register, we assume CPU0 is
	// already done with it
	volatile uint32_t *STG = (void*)STGENC_BASE;
	asm ("mcr p15, 0, %0, c14, c0, 0"::"r"(STG[8]));

//	gicc_enable();
	call_smc(SMC_NSEC,0,0); // switch to NS state
	setup_ns_mode();
	sec_cpu_state = CPUST_ON; 
	sec_boota();
}

__attribute__ ((weak)) int power_off() 
{
	return -1; // not-implemented
}

__attribute__ ((weak)) void reset_hook()
{
}

__attribute__ ((weak)) int reset_machine() 
{
	RCC->MP_GRSTCSETR = RCC_MP_GRSTCSETR_MPSYSRST;
	for (;;) __WFI();
	return 0;
}

uint32_t psci_handler(uint32_t cmd,uint32_t a1,uint32_t a2,uint32_t a3)
{
	if (cmd == 0x84000000) {
		sec_cpu_state = CPUST_OFF; 
		return 0x2;
	}
	if (cmd == 0x84000002) { // CPU_OFF
		sec_cpu_state = CPUST_OFF; 
		__DMB();
		__DSB();
		// RST is done only by subsequent WFI
		// TODO: repowering CPU doesnt work yet
		RCC->MP_GRSTCSETR = RCC_MP_GRSTCSETR_MPUP1RST;
		for (;;) __WFI();
		return -3;
	}
	if (cmd == 0x84000003) { // CPU_ON
		if (!can_boot_sec) return -1; 
		xprintf("start cpu %X %X %X\n",a1,a2,a3);
		sec_boota = (void*)a2;
		if (a1 != 1) return -2;
		sec_cpu_state = CPUST_PEND; 
		const int test = 0;
		if (test) sec_boota = &secondary_test;
		start_cpu2(&boot_secondary);
		return test ? -1 : 0;
	}
	if (cmd == 0x84000004) { // affinity info
		if (a1 != 1) return -2;
		xprintf("affinity %d\n",sec_cpu_state);
		return sec_cpu_state;
	}
	if (cmd == 0x84000008) { // off
		return power_off();
	}
	if (cmd == 0x84000009) { // reset
		reset_hook();
		return reset_machine();
	}
	return -1;
}

void psci_setup(struct boot_param_header *fdt,uint32_t *root)
{
	uint32_t cpu_on = 0;
	fetch_fdt_ints(fdt,root,"psci-cpu-on",1,1,&cpu_on);
	can_boot_sec = cpu_on;
}
