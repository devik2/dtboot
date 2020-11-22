#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "system.h"
#include "xprintf.h"
#include "coro.h"
#include "stm32mp157axx_cm4.h"
#include "stm32mp1_ddr_regs.h"
#include "ddr_regtab.h"

#define _DDRPHYC ((struct stm32mp1_ddrphy *)DDRPHYC_BASE)
#define _DDRC ((struct stm32mp1_ddrctl *)DDRC_BASE)

static char verbose = 0;

const struct ddr_regtab_t *find_reg(const char *name,volatile uint32_t **ptr)
{
	const struct ddr_regtab_t *p = ddr_regtab;
	for (;p->name;p++) 
		if (!strcasecmp(p->name,name)) {
			if (ptr) {
				uint32_t base = p->flags & 1 ? DDRPHYC_BASE : DDRC_BASE;
				*ptr = (void*)(base + p->off);
			}
			return p;
		}
	return NULL;
}

int mod_ddr_reg(const char *name,uint32_t s,uint32_t r)
{
	volatile uint32_t *ptr;
	const struct ddr_regtab_t *reg = find_reg(name,&ptr);
	if (!reg) {
		xprintf("ERR: bad reg '%s'\n",name);
		return -1;
	}
//	xprintf(" _mod '%s': [%08X]\n",name,ptr);
	uint32_t old = *ptr, new = (old & ~r) | s;
	//xprintf("mod '%s': [%08X] %08X->%08X\n",name,ptr,old,new);
	*ptr = new;
	return 0;
}

void mod_flagged(uint16_t msk,int use_ddr3)
{
	msk &= 0xfe00;
	msk |= 0x80;
	const struct ddr_regtab_t *p = ddr_regtab;
	for (;p->name;p++) {
		if ((p->flags & msk) == msk) 
			mod_ddr_reg(p->name,
				use_ddr3 ? p->val3 : p->val, 
				0xffffffff);
	}
}

int idone_wait(struct stm32mp1_ddrphy *phy)
{
	int tmo = 0;
	if (verbose)
		xprintf("[%d] idone wait\n",get_ms_precise());
	udelay(100); // needed when pir is set
	while ((phy->pgsr & (DDRPHYC_PGSR_IDONE|DDRPHYC_PGSR_DTERR|
			DDRPHYC_PGSR_DTIERR|DDRPHYC_PGSR_DFTERR|
			DDRPHYC_PGSR_RVERR|DDRPHYC_PGSR_RVEIRR))==0) {
		if (++tmo > 1000000) {
			xprintf("idone tmo %X\n",phy->pgsr);
			return -1;
		}
		coro_yield();
	}
	if (verbose)
		xprintf("[%d] idone %X\n",get_ms_precise(),phy->pgsr);
	return 0;
}

int swdone_wait(struct stm32mp1_ddrctl *ctl)
{
	int tmo = 0;
	mod_ddr_reg("swctl",DDRCTRL_SWCTL_SW_DONE,0);
	while (!(ctl->swstat & 1)) {
		if (++tmo > 1000000) {
			xprintf("swdone tmo %X\n",ctl->swstat);
			return -1;
		}
		coro_yield();
	}
	if (verbose)
		xprintf("[%d] swdone %X\n",get_ms_precise(),ctl->swstat);
	return 0;
}

int mode_wait(struct stm32mp1_ddrctl *ctl,int mode)
{
	int tmo = 0;
	while ((ctl->stat & 0x7)!=mode) {
		if (++tmo > 1000000) {
			xprintf("mode wait tmo %X\n",ctl->stat);
			return -1;
		}
		coro_yield();
	}
	if (verbose)
		xprintf("[%d] mode done %X\n",get_ms_precise(),ctl->stat);
	return 0;
}

void set_dcu_row(int row,uint16_t a,int ba,uint32_t dt,int cmd)
{
	_DDRPHYC->dcuar = row|0x400;
	_DDRPHYC->dcudr = dt;
	_DDRPHYC->dcudr = (a<<4)|(ba<<20)|(cmd<<23);
	_DDRPHYC->dcudr = 0;
	_DDRPHYC->dcudr = 0;
	_DDRPHYC->dcurr = 1;
	xprintf("DCU sts %X\n",_DDRPHYC->dcusr0);
}

void ddr3_params(int mhz)
{
}

// returns mem sz in MB
int mem_meas(volatile uint32_t *addr,int max_mb)
{
	uint32_t i,j;
	coro_yield();
	for (j=1;j<=max_mb;j<<=1) {
//		xprintf(" try DDR sz %d MB\n",j);
		uint32_t xtra = j<<16;
		for (i=0;i<j;i++) {
			uint32_t off = 1024*1024/4*i;
			addr[off] = i|xtra;
			addr[off+1] = ~(i|xtra);
		}
		for (i=0;i<j;i++) {
			uint32_t off = 1024*1024/4*i;
			if (addr[off] != (i|xtra) ||
				addr[off+1] != ~(i|xtra)) return j>>1;
		}
	}
	return j>>1;
}

int mem_test(volatile uint32_t *addr,int mb,uint32_t xor,int verb)
{
	uint32_t i,cnt = 1024*1024*mb/4,errs=0;
	uint32_t t1 = get_ms_precise();
	coro_yield();
	for (i=0;i<cnt;i++) {
//		fpga_gpio2(i==0x3458); // trig
		addr[i] = i^xor;
#if 0
		uint32_t d = addr[i];
		if (d != i^xor) {
			if ((verb&0xff)>0) {
				xprintf("werr at %08X is %08X should be %08X\n",i,d,i^xor);
				verb--;
			}
		}
#endif
	}
	coro_yield();
	uint32_t tmsk = 0xffffffff;
	for (i=0;i<cnt;i++) {
		uint32_t d = addr[i];
		if (((d ^ (i^xor))&tmsk)==0) continue;
		errs++;
		if ((verb&0xff)>1) {
			xprintf("merr at %08X is %08X should be %08X\n",i,d,i^xor);
			verb--;
		}
	}
	if (verb) {
		uint32_t t2 = get_ms_precise();
		xprintf("mem errs xor %08X is %d in %d ms\n",xor,errs,t2-t1);
	}
	return errs;
}

int ddr_init(int use_slow,int use_ddr3,int fast_test)
{
	// reset all blocks
	RCC->DDRITFCR |= RCC_DDRITFCR_DPHYCTLRST|RCC_DDRITFCR_DPHYRST|
		RCC_DDRITFCR_DPHYAPBRST|RCC_DDRITFCR_DDRCORERST|
		RCC_DDRITFCR_DDRCAXIRST|RCC_DDRITFCR_DDRCAPBRST;

	RCC->DDRITFCR |= 0x6ff; // turn all DDR clocks on

	// deassert PHY resets
	RCC->DDRITFCR &= ~(RCC_DDRITFCR_DPHYCTLRST|RCC_DDRITFCR_DPHYRST);
	RCC->DDRITFCR &= ~RCC_DDRITFCR_DDRCAPBRST;

	xprintf("DDR clocks on, ddr3=%d slow=%d\n",use_ddr3,use_slow);
	udelay(100);
	/* Stop uMCTL2 before PHY is ready */
	mod_ddr_reg("dfimisc",0,DDRCTRL_DFIMISC_DFI_INIT_COMPLETE_EN);
	mod_flagged(0x300,use_ddr3);
	mod_flagged(0x500,use_ddr3);
	mod_flagged(0x900,use_ddr3); // amap

	/* skip CTRL init, SDRAM init is done by PHY PUBL */
	mod_ddr_reg("init0",DDRCTRL_INIT0_SKIP_DRAM_INIT_NORMAL,
			DDRCTRL_INIT0_SKIP_DRAM_INIT_MASK);
	mod_flagged(0x1100,use_ddr3);

	/*  2. deassert reset signal core_ddrc_rstn, aresetn and presetn */
	RCC->DDRITFCR &= ~(RCC_DDRITFCR_DDRCORERST|RCC_DDRITFCR_DDRCAXIRST
			|RCC_DDRITFCR_DPHYAPBRST);
	udelay(1000);

	/*  3. start PHY init by accessing relevant PUBL registers
	 *    (DXGCR, DCR, PTR*, MR*, DTPR*) */
	mod_flagged(0x2100,use_ddr3);
	mod_flagged(0x4100,use_ddr3);
	mod_flagged(0x8100,use_ddr3);
	idone_wait(_DDRPHYC);

	/*  5. Indicate to PUBL that controller performs SDRAM initialization
	 *     by setting PIR.INIT and PIR CTLDINIT and pool PGSR.IDONE
	 *     DRAM init is done by PHY, init0.skip_dram.init = 1 */
	uint32_t pir = DDRPHYC_PIR_DLLSRST | 
		DDRPHYC_PIR_ZCAL | DDRPHYC_PIR_ITMSRST | DDRPHYC_PIR_DRAMINIT | 
		DDRPHYC_PIR_ICPC | DDRPHYC_PIR_INIT;
	if (use_slow&0) pir |= BIT(17);  // DLLBYP
	else pir |= DDRPHYC_PIR_DLLLOCK|DDRPHYC_PIR_QSTRN;
	if (use_ddr3) pir |= DDRPHYC_PIR_DRAMRST;

	mod_ddr_reg("pir",pir,0xffffffff);
	idone_wait(_DDRPHYC);

	/*  6. SET DFIMISC.dfi_init_complete_en to 1 */
	/* Enable quasi-dynamic register programming*/
	mod_ddr_reg("swctl",0,DDRCTRL_SWCTL_SW_DONE);
	mod_ddr_reg("dfimisc",DDRCTRL_DFIMISC_DFI_INIT_COMPLETE_EN,0);
	swdone_wait(_DDRC);

	/*  7. Wait for DWC_ddr_umctl2 to move to normal operation mode
	 *     by monitoring STAT.operating_mode signal */
	/* wait uMCTL2 ready */
	mode_wait(_DDRC,DDRCTRL_STAT_OPERATING_MODE_NORMAL);

	for (;0;) {
		set_dcu_row(0,7,0,77,8);
	}
	xprintf("[%d] enable DDR ports\n",get_ms_precise());
	mod_ddr_reg("pctrl_0",DDRCTRL_PCTRL_N_PORT_EN,0);
	mod_ddr_reg("pctrl_1",DDRCTRL_PCTRL_N_PORT_EN,0);

	for(;0;) {
		mem_test((void*)0xc0000000,8,-1,10);
		mem_test((void*)0xc0000000,8,0,10);
	}
	int mb = 0;
	if (fast_test || !mem_test((void*)0xc0000000,1,0,0x10a)) 
		mb = mem_meas((void*)0xc0000000,512);
	xprintf("[%d] detected %d MB DDR\n",get_ms_precise(),mb);
	return mb;
}
	
