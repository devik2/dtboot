#include <stdint.h>
#include "xprintf.h"
#include "system.h"
#include "stm32mp1.h"


__attribute__ ((weak)) int power_off() 
{
	return -1; // not-implemented
}

__attribute__ ((weak)) void reset_hook()
{
}

__attribute__ ((weak)) int reset_machine() 
{
	RCC->GRSTCSETR = RCC_GRSTCSETR_SYSRST;
	for (;;) __WFI();
	return 0;
}

void dbg_print(const char *s,unsigned x,const char *e);
uint32_t psci_handler(uint32_t cmd,uintptr_t a1,uintptr_t a2,uintptr_t a3)
{
	if (cmd == 0x84000000) return 0x10000; // PSCI ver
	if (cmd == 0x8400000a) { // PSCI feats (Linux queries SMCCC here too)
		if ((a1 & 0xfffffffe) == 0x80000000) return 0;
		return -1;
	}
	if (cmd == 0xC4000003) { // CPU_ON (SMC64, entry point is 64-bit)
		dbg_print("CPU ON ",a1,"\r\n");
		if ((a1 & 0xff) != 1) return -2; // only CPU1 supported
		extern volatile uintptr_t cpu1_entry;
		cpu1_entry = a2;
		__DSB();
		asm volatile("sev");
		return 0;
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

