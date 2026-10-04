#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "system.h"
#include "xprintf.h"
#include "ca7.h"

#define GICD_SGIR               0x0f00
#define GICD_CTLR               0x0000
#define GICD_TYPER              0x0004
#define GICD_IGROUPRn           0x0080

#define CPUST_ON 0
#define CPUST_OFF 1
#define CPUST_PEND 2

__attribute__((section(".text.early")))
static void stm32mp_init_gic_local()
{
	// SGIs (0-15) + PPIs (16-31) are banked per-CPU — set all as non-secure group 1
	GICDistributor->IGROUPR[0] = 0xffffffff;
	// CPU interface priority mask: allow all priorities through to NS
	GICInterface->PMR = 0xff;
	// enable CPU interface for Group1 only (our SGIs) so they can wake from WFI
	GICInterface->CTLR = 2;
}

__attribute__((section(".text.early")))
static void stm32mp_init_gic()
{
	// enable distributor for both secure and non-secure (bits 0+1)
	GICDistributor->CTLR |= 3;
	// set shared SPIs (IRQ 32+) as non-secure group 1; IGROUPR[0] is banked, done in local init
	int irq_cnt = GICDistributor->TYPER & 0x1f, i;
	for (i = 1; i <= irq_cnt; i++)
		GICDistributor->IGROUPR[i] = 0xffffffff;
	stm32mp_init_gic_local();
}

#define DBG_PRINT 1

const static char hex_tab[16] = "0123456789ABCDEF";
#if DBG_PRINT
// debug print routines
// secure subsys can't use xprintf, use direct UART writes and assume
// someone inited the UART (debug usable only from later SMC calls)
static void dbg_char(const char c)
{
	USART_TypeDef *U = LPUART1;
	if (!(U->CR1 & USART_CR1_UE)) return;
	int tmo = 0;
	while (!(U->ISR & USART_ISR_TXE_TXFNF))
		if (++tmo > 100000) break;
	U->TDR = c;
}

static int to_hex(char *tgt,unsigned x)
{
	int i,emit=0,n=0;
	for (i=0;i<8;i++,x<<=4) {
		int v = x>>28;
		emit |= v;
		if (emit || i==7) tgt[n++] = hex_tab[v];
	}
	return n;
}

void dbg_print(const char *s,unsigned x,const char *e)
{
	char buf[16];
	buf[to_hex(buf,x)] = 0;
	while (*s) dbg_char(*(s++));
	s = buf;
	while (*s) dbg_char(*(s++));
	while (*e) dbg_char(*(e++));
}
#else
void dbg_print(const char *s,unsigned x,const char *e) {}
#endif

int get_SP();
int get_SCR();
int get_SCTLR3();
int get_VBAR1();
int get_VBAR3();
extern int p_scr;
uint32_t psci_handler(uint32_t cmd,uintptr_t a1,uintptr_t a2,uintptr_t a3);

__attribute__ ((weak))
uint32_t smc_mach_hook(uint32_t cmd,uintptr_t a1,uintptr_t a2,uintptr_t a3)
{
	return -1;
}

void invoke_scmi(uint32_t t1,uint32_t t2);
void scmi_init();

// called in secure mode because it is not available later
// TODO: probably 0xdeb6.... must be used ?
extern uint32_t dbg_idc,rpn,cpu_id[3];
static void cache_dbg_idc_register()
{
	unsigned den = BSEC->DENR;
	BSEC->DENR |= BSEC_DENR_DBGSWEN;	// enable reading DBG
	// APBDBG clk should be on by default
	(void)BSEC->DENR;
	dbg_idc = DEVICE_ID();
	rpn = *(uint32_t*)0x44000024;
	cpu_id[0] = *(uint32_t*)(0x44000000+20);
	cpu_id[1] = *(uint32_t*)(0x44000000+24);
	cpu_id[2] = *(uint32_t*)(0x44000000+28);
	BSEC->DENR = den;
}

void etm_init();

__attribute__((section(".text.early")))
static void enable_all_dbg()
{
	BSEC->DENR = 0xdeb60ffe;
}

static uint32_t chk_iac()
{
	int n = 0;
	for (int i=0;i<6;i++) {
		uint32_t v = IAC->ISR[i];
		if (!v) continue;
		IAC->ICR[i] = v;
		dbg_print("\nIAC ",i,"");
		dbg_print(" ",v,"\r\n");
		n++;
	}
	return n;
}

volatile uintptr_t cpu1_entry;

uint32_t smc_handler(uint32_t cmd,uintptr_t a1,uintptr_t a2,uintptr_t a3)
{
#if 0
	xprintf("[%d] SMC %X %X %X %X SP %X pscr:%X\n",
			// for SMC_SET_FREQ time may be imprecise for moment
			cmd == SMC_SET_FREQ ? 0:get_ms_precise(),
			cmd,a1,a2,a3,get_SP(),p_scr);

	if (cmd == SMC_DUMP_INFO) {
		dump_reg("SCTLR",get_SCTLR3(),_bn_sctlr);
		dump_reg("SCR",p_scr,_bn_scr);
		xprintf("VBAR3:%X VBAR1:%X\n",get_VBAR3(),get_VBAR1());
		return 0;
	}
#endif
	// init SCMI during some PSCI call, 
	// it would be too late in invoke_scmi
	scmi_init();

	if (cmd == SMC_CHK_IAC) return chk_iac();

	// STGEN is accessible only from secure mode
	if (cmd == SMC_SET_STGEN) {
		STGENC->CNTCR = 0;
		asm ("msr CNTFRQ_EL0, %0"::"r"(a1));
		STGENC->CNTFID0 = a1;
		STGENC->CNTCVL = a2; STGENC->CNTCVU = 0;
		STGENC->CNTCR = 1;
		return 0;
	}
	//dbg_print("\nSMC ",cmd,"\r\n");
	if (cmd == 0x80000000) return 0x10001; // SMCCC version
	if (cmd == 0x80000001) { // feats
		if ((a1 & 0xfffffffe) == 0x80000000) return 0;
		return -1;
	}
	if (cmd == 0xc3000001) {
		invoke_scmi(a1,a2);
		return 0;
	}
	if ((cmd & 0xbf000000) == 0x84000000)
		return psci_handler(cmd,a1,a2,a3);
#if 0
	if ((cmd & 0xff00ff00) == SMC_MACH)
		return smc_mach_hook(cmd,a1,a2,a3);
#endif
	return -1;
}

uint32_t call_smc_el3(uint32_t cmd,uintptr_t a1,uint32_t a2);
void main_el1();

// MP2 OTP layout: OTP247=mac1[0..3], OTP248=mac2[0..1]|mac1[4..5], OTP249=mac2[2..5]
extern uint8_t cached_mac_addr[2][6];
static void cache_mac_address(void)
{
	uint32_t w247 = BSEC->FVRw[247];
	uint32_t w248 = BSEC->FVRw[248];
	uint32_t w249 = BSEC->FVRw[249];

	memcpy(cached_mac_addr[0], &w247, 4);
	cached_mac_addr[0][4] = w248 & 0xff;
	cached_mac_addr[0][5] = (w248 >> 8) & 0xff;

	cached_mac_addr[1][0] = (w248 >> 16) & 0xff;
	cached_mac_addr[1][1] = (w248 >> 24) & 0xff;
	memcpy(cached_mac_addr[1] + 2, &w249, 4);
}

uint32_t _dbg_penhold;

__attribute__((section(".text.early")))
void main_core1(void)
{
	stm32mp_init_gic_local();
	while (!cpu1_entry) {
		_dbg_penhold++;
		asm volatile ("wfe");
	}
	call_smc_el3(SMC_NSEC, cpu1_entry, 0);
}

__attribute__((section(".text.early")))
static void risab_as_NS(RISAB_TypeDef *R,int start)
{
	R->CR = RISAB_CR_SRWIAD;	// allow also SEC access
	for (int i=start;i<32;i++) {
		R->PGSECCFGR[i] = 0;	// set blocks as NS
		// TODO: MDMA fails CID checks, seems to use CID 0 instead of 1
		R->PGCIDCFGR[i] = 0;	// disable CID checks
	}
}

#define ENCLK(X) RCC->X = 6; (void)RCC->X

// called early to extend secure SRAM memory; or it will
// fail to fetch insn at 0xe004000
__attribute__((section(".text.early")))
void prep_risab1()
{
	enable_all_dbg();
	for(volatile int i=0;i<1000000;i++);

	ENCLK(SRAM1CFGR);
	ENCLK(SRAM2CFGR);

	// set SYSRAM below _el1_vect as secure
	extern char _el1_vect[];
	uint32_t water = (uint32_t)_el1_vect & 0xffff;
	risab_as_NS(RISAB1,(water+4095)/4096);
	int i;
	for (i=0;water > 0xfff; water -= 0x1000, i++) 
		RISAB1->PGSECCFGR[i] = 0xff; // whole pages
	if (water)
		RISAB1->PGSECCFGR[i] = 0x0f; // extra half page

}

// we are entering in EL3
void main(const boot_api_context_t *ctx)
{
	// assume all power pins are on
	PWR->CR1 |= PWR_CR1_VDDIO3SV|PWR_CR1_VDDIO3VMEN|PWR_CR1_VDDIO3VRSEL*0;
	PWR->CR1 |= PWR_CR1_VDDIO4SV|PWR_CR1_VDDIO4VMEN;
	PWR->CR8 |= PWR_CR8_VDDIO1SV|PWR_CR8_VDDIO1VMEN;
	PWR->CR7 |= PWR_CR7_VDDIO2SV|PWR_CR7_VDDIO2VMEN;

	// secure only peripheral
	ENCLK(STGENCFGR);
#if DBG_PRINT
	ENCLK(LPUART1CFGR);
#endif
	PWR->BDCR1 = 1; // EN D3 domain writes
	int i;
	for (i=0;i<12;i++) {
		volatile uint32_t *cf = &RCC->GPIOACFGR+i;
		*cf |= 2;
		(void)*cf;
	}
	GPIO_TypeDef *G = GPIOA;
	for (;G <= GPIOK;G=((void*)G)+(GPIOB_BASE-GPIOA_BASE))
		G->SECCFGR = 0;
	GPIOZ->SECCFGR = 0;
	// enable RAMs to set RISAxx later
	ENCLK(DDRCPCFGR);
	if (0) {
		RCC->DBGCFGR |= RCC_DBGCFGR_DBGEN|RCC_DBGCFGR_TRACEEN;
		ENCLK(STMCFGR);
		ENCLK(ETRCFGR);
		etm_init();
	}

	// allow NS access to PLL1 TODO: relock before linux start ?
	// Note: NS_ENABLE_0_RW is wrong in header typedef, use direct address
	((volatile uint32_t*)CA35SSC)[240] = 0xffffffff;

	// allow kernel access to M33 sys regs
	CA35SYSCFG->M33_ACCESS_CR = 0; // TODO: should be PRIV but is doesn't work
	CA35SYSCFG->M33_TZEN_CR = 0;

	risab_as_NS(RISAB2,0);
	risab_as_NS(RISAB3,0);
	risab_as_NS(RISAB4,0);
	risab_as_NS(RISAB5,0);

	// NS access to DDR
	RISAF4->REG[0].ENDR = 0xffffffff;
	RISAF4->REG[0].CIDCFGR = 0xff00ff;
	RISAF4->REG[0].CFGR = 1;

	stm32mp_init_gic();
	cache_dbg_idc_register();
	cache_mac_address();
//		_halt();

	call_smc_el3(SMC_NSEC,(uintptr_t)main_el1,0); // switch to NS state
}
