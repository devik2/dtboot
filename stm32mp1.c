#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "xprintf.h"
#include "coro.h"
#include "gpio.h"
#include "system.h"
#include "fdt.h"
#include "stm32mp1_ddr_regs.h"

#define S_GPIO_MODER(G,P,M) S_BITFLD(G->MODER,P*2,2,M);
#define S_GPIO_OSPEEDR(G,P,S) S_BITFLD(G->OSPEEDR,P*2,2,S);
// zapne ALT funkci N na pinu P portu G
#define S_GPIO_ALT(G,P,N) S_BITFLD(G->AFR[(P>>3)&1],(P&7)*4,4,N); \
			S_GPIO_MODER(G,P,2)

// first part, if LSE not running start it, it can take 2sec
// to start so that dont wait for it
static uint32_t lse_start_tm;
static void check_lse()
{
	PWR->CR1 |= PWR_CR1_DBP; 
	if (!(RCC->BDCR & RCC_BDCR_LSERDY)) {
		RCC->BDCR |= RCC_BDCR_LSEON;
		xprintf("enable LSE\n");
		lse_start_tm = get_ms();
	}
}

static void check_rtc()
{
	while (!(RCC->BDCR & RCC_BDCR_LSERDY)) {
		if (seq_after(get_ms(),lse_start_tm+2000)) {
			xprintf("can't start LSE\n");
			return;
		}
	}
	if (!(RCC->BDCR & RCC_BDCR_RTCCKEN)) {
		xprintf("enable RTC CK\n");
		RCC->BDCR |= RCC_BDCR_RTCSRC_1|RCC_BDCR_RTCCKEN;
	}
}

// try to wait for external or xtal clock, returns 0 if ok
static int hse_on(int ext)
{
	uint32_t conf = RCC_OCENSETR_HSEON;
	if (ext) conf |= RCC_OCENSETR_HSEBYP|RCC_OCENSETR_DIGBYP;
	RCC->OCENSETR = conf;
	uint32_t tm = get_ms();
	while (!(RCC->OCRDYR & RCC_OCRDYR_HSERDY)) {
		if (get_ms()>tm+20) {
			// revert ext clock settings
			RCC->OCENCLRR = conf;
			return -1;
		}
		coro_yield();
	}
	RCC->CPERCKSELR = 2; // use HSE as per_ck
	xprintf("HSE (ext=%d) on in %d ms\n",ext,get_ms()-tm);
	return 0;
}

static void csi_on()
{
	RCC->OCENSETR = RCC_OCENSETR_CSION;
	while (!(RCC->OCRDYR & RCC_OCRDYR_CSIRDY)) coro_yield();
	xprintf("CSI on\n");
}

static void compensation_on()
{
	RCC->MP_APB3ENSETR = RCC_MC_APB3ENSETR_SYSCFGEN;
	SYSCFG->CMPENSETR = 1;
	while (!(SYSCFG->CMPCR & SYSCFG_CMPCR_READY)) coro_yield();
	SYSCFG->CMPCR &= ~SYSCFG_CMPCR_SW_CTRL;
	xprintf("COMPensation on\n");
}

static const uint32_t pll_btab[4] = { 0x80,0x94,0x880,0x894 };

static void stop_pll(int n)
{
	volatile uint32_t *P = (uint32_t*)((char*)RCC_BASE + pll_btab[n-1]);
	P[0] = 3; // disable dividers
	P[0] = 2;
	while (P[0] & 2) coro_yield();
}

static const uint16_t pll_minin[4] = { 8000,8000,4000,4000 };

// returns vcof if all ok, -x for err
int set_pll(int n,int fi,int m,int mul,int dp,int dq,int dr)
{
	if (n<1 || n>4) return -11;
	// inp. 1,2:8-16M, 3,4:4-16M
	if (fi < m*pll_minin[n-1] || fi > m*16000) return -12;
	if (mul<25 || mul>(n<=1?100:200)) return -13;
	int fvco = mul*fi/m; // must be 400..800MHz
	if (fvco<400000 || fvco>800000) return -14;
	if (dp<0 || dp>128 || dq<0 || dq>128 || dr<0 || dr>128) return -15;

	volatile uint32_t *P = (uint32_t*)((char*)RCC_BASE + pll_btab[n-1]);
	uint32_t ctl = 0;
	if (P[0] & 3) stop_pll(n); 
	//xprintf("PLL sts %x\n",P[0]);
	if (dp) ctl |= 0x10;
	if (dq) ctl |= 0x20;
	if (dr) ctl |= 0x40;
	P[1] = ((m-1)<<16)|(mul-1);
	P[2] = (dp?dp-1:0)|((dq?dq-1:0)<<8)|((dr?dr-1:0)<<16);
	P[0] = ctl|1; // power on
	while (!(P[0] & 2)) coro_yield();
	xprintf("PLL %d on, vco=%d p=%d q=%d r=%d\n",n,fvco,
			dp?fvco/dp:0,dq?fvco/dq:0,dr?fvco/dr:0);
	return fvco;
}

static uint32_t fq1e3_cache,fq1e6_cache;

uint32_t call_smc(uint32_t cmd,uint32_t a1,uint32_t a2);
void stgen_setup(int khz,int use_hse,int keep)
{
	RCC->STGENCKSELR = use_hse;
	volatile uint32_t *STG = (void*)STGENC_BASE;
	STG[0] = 0; // stop
	int hz = khz*1000;
	uint64_t x = 0;
	if (keep) { // assume STG[3] is zero (valid for boot time<1min)
		x = STG[2];
		x = x*hz/STG[8]; // recompute time for new clock
	}
	STG[2] = x; STG[3] = 0;
	STG[8] = hz;
#if NSEC
	int nons = ((struct stm32mp1_mctx*)mctx)->boot_flags[BFI_NONS];
	if (nons<0) nons = 0;
	if (!(nons & 1))
		call_smc(SMC_SET_FREQ,hz,0);
	else
#endif
		asm ("mcr p15, 0, %0, c14, c0, 0"::"r"(hz));
	STG[0] = 1; // start
	for (ms_freq_sh = 0;hz;ms_freq_sh++) hz >>= 1;
	ms_freq_sh -= 10;
	fq1e6_cache = fq1e3_cache = 0;
	uint64_t t = get_arm_time();
	uint32_t tf = get_arm_tfreq();
	xprintf("[%d] delay tmr %X %X @%d\n",get_ms_precise(),
			(int)get_arm_time(),(int)(t>>32),tf);
}

uint8_t ms_freq_sh;		// approx shift for ms cnv

uint32_t get_ms()
{
	return get_arm_time() >> ms_freq_sh;
}

uint64_t get_us_precise()
{
	uint64_t tm = get_arm_time();
	if (!fq1e6_cache) fq1e6_cache = get_arm_tfreq()/1000000;
	return tm/fq1e6_cache;
}

uint32_t get_ms_precise()
{
	uint64_t tm = get_arm_time();
	if (!fq1e3_cache) fq1e3_cache = get_arm_tfreq()/1000;
	return tm/fq1e3_cache;
}

void udelay(uint32_t us)
{
#if 0
	int i;
	for (i=0;i<30*us;i++) 
		__asm("nop");
#else
	uint32_t f = get_arm_tfreq()>>20;
	uint64_t tgt = get_arm_time() + f*us;
	while (seq64_after(tgt,get_arm_time())) coro_yield();
#endif
}

// p & len must be 32bit aligned, max len is 64k
static void crc_dma(const char *p,int len,int rpt)
{
	MDMA_Channel1->CIFCR = 0x1f;	// clear all flags
	MDMA_Channel1->CSAR = (int)p;
	MDMA_Channel1->CDAR = (int)&CRC1->DR;
	MDMA_Channel1->CBNDTR = len|(rpt<<MDMA_CBNDTR_BRC_Pos);
	MDMA_Channel1->CTCR = ((4-1)<<MDMA_CTCR_TLEN_Pos)|
		MDMA_CTCR_DINCOS_1|MDMA_CTCR_SINCOS_1|
		//MDMA_CTCR_DBURST_0|MDMA_CTCR_SBURST_0|
		MDMA_CTCR_DSIZE_1|MDMA_CTCR_SSIZE_1|MDMA_CTCR_SINC_1|
		MDMA_CTCR_TRGM_1|MDMA_CTCR_SWRM;
	MDMA_Channel1->CCR = MDMA_CCR_EN;
	MDMA_Channel1->CCR = MDMA_CCR_EN|MDMA_CCR_SWRQ;
	while (!(MDMA_Channel1->CISR & MDMA_CISR_CTCIF)) coro_yield();
}

// fast HW checker
uint32_t crc32 (uint32_t crc, const void *p, uint32_t len)
{
	int dma = 1;
	CRC1->INIT = ~crc;
	CRC1->CR = CRC_CR_RESET|CRC_CR_REV_OUT|CRC_CR_REV_IN_0|CRC_CR_REV_IN_1;
	if ((((int)p) & 3) == 0) { // aligned
		if (len>=0x10000 && dma) {
			crc_dma(p,0x10000,(len>>16)-1);
			p += len & 0xfff0000;
			len &= 0xffff;
		}
		if (len<128 || !dma) {
			while (len>=4) {
				CRC1->DR = *(int*)p;
				len-=4; p+=4;
			}
		} else {
			crc_dma(p,len & 0xfffc,0);
			p += len & 0xfffc;
			len &= 3;
		}
	}
	CRC1->CR = CRC_CR_REV_OUT|CRC_CR_REV_IN_0;
	while (len>0) {
		*(volatile char*)&CRC1->DR = *(const char*)p;
		len--; p++;
	}
	uint32_t co = ~CRC1->DR;
	return co;
}

// allow port Z and DDR be accessed from nonsec mode
void set_tz_sec() 
{
	RCC->MP_AHB5ENSETR = RCC_MC_AHB5ENSETR_GPIOZEN;
	(void)RCC->MP_AHB5ENSETR;	// dummy read
	GPIOZ->SECR = 0;		// port Z as unsecure

	RCC->MP_APB5ENSETR = RCC_MC_APB5ENSETR_TZPCEN|RCC_MC_APB5ENSETR_TZC1EN|
		RCC_MC_APB5ENSETR_TZC2EN;
	(void)RCC->MP_APB5ENSETR;
	TZPC->DECPROT0 = 0x3ffffff;
	TZPC->TZMA1_SIZE = 0;
	TZC->REG_ID_ACCESSO =  0xffffffff;
	TZC->REG_ATTRIBUTESO = 0xc0000003;
	TZC->GATE_KEEPER = 3;
}

static int mp1_check_flags(const uint8_t *flg)
{
	uint8_t sum = 0;
	for (;*flg & 0xf0;flg++) sum ^= *flg;
	sum ^= sum>>4;
	return (*flg & 0xf) == (sum & 0xf);
}

static int mp1_apply_flags(uint8_t *tab,const uint8_t *flg)
{
	if (!mp1_check_flags(flg)) return 0;
	for (;*flg & 0xf0;flg++) 
		tab[(*flg>>4) & 0xf] = *flg & 0xf;
	return 1;
}

extern const uint8_t ebflags[];
void mp1_read_boot_flags(struct stm32mp1_mctx *ctx)
{
	memset(ctx->boot_flags,0xff,sizeof(ctx->boot_flags));
	mp1_apply_flags(ctx->boot_flags,ebflags);
	uint32_t lf[BOOT_FLAGS_MAX],i;
	volatile uint32_t *t = &TAMP->BKP0R;
	for (i=0;i<BOOT_FLAGS_MAX;i++) lf[i] = t[BOOT_FLAGS_POS+i];
	mp1_apply_flags(ctx->boot_flags,(uint8_t*)lf);

	i = t[BOOT_MODE_INSN];
	ctx->boot_insn = 0;
	if ((i & 0xffffff00) == 0x11770000) ctx->boot_insn = i & 255;
}

void mp1_set_boot_insn(uint8_t insn)
{
	PWR->CR1 |= PWR_CR1_DBP; 
	volatile uint32_t *t = &TAMP->BKP0R;
	t[BOOT_MODE_INSN] = 0x11770000|insn;
}

void mp1_show_boot_flags()
{
	struct stm32mp1_mctx *ctx = (struct stm32mp1_mctx *)mctx;
	char buf[80]="bflags:",*p=buf+7;
	int i;
	for (i=1;i<16;i++) {
		if (ctx->boot_flags[i]<0) continue;
		p += xsnprintf(p,5," %x=%x",i,ctx->boot_flags[i]);
	}
	xprintf("INSN:%d %s\n",ctx->boot_insn,buf);
}

void i2c_setup(I2C_TypeDef *I2C)
{
	I2C->CR1 = 0;
	I2C->TIMINGR = 0x10ffffff;
	I2C->CR1 = I2C_CR1_PE;
}

// aut is auto-end, use when no trailing read is needed
int i2c_wr(I2C_TypeDef *I2C,int addr,const char *data,int len,int aut)
{
	I2C->ICR = I2C_ICR_STOPCF|I2C_ICR_NACKCF;
	I2C->CR2 = (addr<<1)|I2C_CR2_START|(aut?I2C_CR2_AUTOEND:0)|(len<<16);
	int i,tmo;
	for (i=0;i<len;i++) {
		tmo = 100000;
		while (!(I2C->ISR & 1)) {
			if (I2C->ISR & I2C_ISR_NACKF) {
				xprintf("I2C NACK %x\n",addr);
				return -1;
			}
			if (!--tmo) {
				xprintf("I2C WTMO %x %x\n",addr,I2C->ISR);
				return -1;
			}
			coro_yield();
		}
		I2C->TXDR = *(data++);
	}
	i = 0; tmo = 100000;
	if (!aut) {
		while (!(I2C->ISR & I2C_ISR_TC)) {
			if (!--tmo) {
				xprintf("I2C WTMO2 %x %x\n",addr,I2C->ISR);
				return -1;
			}
			coro_yield();
		}
	} else {
		udelay(100); // weird - needed not to miss busy sometimes
		while (I2C->ISR & I2C_ISR_BUSY) {
			if (!--tmo) {
				xprintf("I2C WTMO3 %x %x\n",addr,I2C->ISR);
				return -1;
			}
			coro_yield();
		}
		udelay(100);
	}
//	if (aut) xprintf("wr done, sts %x %d\n",I2C->ISR,i);
	return 0;
}

int i2c_rd(I2C_TypeDef *I2C,int addr,char *buf,int len)
{
	I2C->ICR = I2C_ICR_STOPCF|I2C_ICR_NACKCF;
	I2C->CR2 = (addr<<1)|I2C_CR2_START|I2C_CR2_AUTOEND|I2C_CR2_RD_WRN|(len<<16);
	int i;
	for (i=0;i<len;i++) {
		int tmo = 100000;
		while (!(I2C->ISR & I2C_ISR_RXNE)) {
			if (I2C->ISR & I2C_ISR_NACKF) {
				xprintf("I2C RNACK %x\n",addr);
				return -1;
			}
			if (!--tmo) {
				xprintf("I2C RTMO %x %x\n",addr,I2C->ISR);
				return -1;
			}
			coro_yield();
		}
		buf[i] = I2C->RXDR;
	}
	return 0;
}

// GPIO support
static GPIO_TypeDef *gpio_get(uint16_t port)
{
	if ((port & 0xf0) == 0xf0) return (GPIO_TypeDef*)GPIOZ_BASE;
	return (GPIO_TypeDef*)(GPIOA_BASE+0x1000*((port>>4)-1));
}

int gpio_in(gport_t port)
{
	GPIO_TypeDef *g = gpio_get(port);
	return (g->IDR >> (port&15)) & 1;
}

void gpio_out(gport_t port,int val)
{
	GPIO_TypeDef *g = gpio_get(port);
	int msk = 1 << (port&15);
	g->BSRR = (msk<<16)|(val ? msk:0);
}

void gpio_setup_one(gport_t port,uint16_t mode,uint8_t alt)
{
	GPIO_TypeDef *g = gpio_get(port);
	//xprintf("port %X addr %X to %X %d\n",port,g,mode,alt);
	const char modes[4] = { 3,0,1,3 };
	S_GPIO_MODER(g,(port&15),mode & PM_ALT ? 2 : modes[mode & 3]);
	if ((mode & 3) == PM_OUT || mode & PM_ALT) {
		S_GPIO_OSPEEDR(g,(port&15),(mode >> PM_SPD_Pos) & 3);
		S_BITFLD(g->OTYPER,(port&15),1,mode & PM_OD ? 1:0);
	}
	if ((mode & 3) == PM_IN || mode & PM_ALT) {
		S_BITFLD(g->PUPDR,(port&15)*2,2,mode & PM_PU ? 1:
				mode & PM_PD ? 2 :0);
	}
	if (mode & PM_ALT) {
		S_GPIO_ALT(g,(port&15),alt); // macro with 2 cmds !
	} else {
		int dflt = (mode >> PM_DFLT_Pos) & 3;
		if (dflt) gpio_out(port,dflt & 1);
	}
}

void gpio_setup(const struct gpio_setup_t *p,int cnt)
{
	for (;p->port && cnt;p++,cnt--) gpio_setup_one(p->port,p->mode,p->alt);
}

void gpio_setup_same(const gport_t *lst,uint16_t mode,uint8_t alt)
{
	for (;*lst;lst++) gpio_setup_one(*lst,mode,alt);
}

#define PIN2_OSCEN PM_Z(5)

// SOMP1 uses pulldown on OSC EN but OSC's internal pullup is
// stronger. Fortunately one can force it down and OSC removes
// it pullup. Do it before suspend (5mA goes to OSC)
void somp1_disable_osc()
{
	gpio_setup_one(PIN2_OSCEN,PM_OUT|PM_DFLT(0),0);
}

static int run_hse(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	struct stm32mp1_mctx *ctx = (struct stm32mp1_mctx *)mctx;

	// use HSE style to detect board version
	ctx->lump_rev = 1;

	// try external osc first (sugested in MP1 errata instead
	// of xtal)
	gpio_setup_one(PIN2_OSCEN,PM_OUT|PM_DFLT(1),0);
	if (hse_on(1)) { 
		ctx->lump_rev = 0;
		if (hse_on(0)) { // old board with xtal
			xprintf("Can start no HSE\n");
			return -1;
		}
	}
	return 0;
}

static int run_csi_comp(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	csi_on();
	compensation_on();
	return 0;
}

void qspi_set_divider(int div);
static int run_clocks(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	uint32_t hse_fq = 0; // 0 means HSI
	fetch_fdt_ints(fdt,root,"mp1,hse-khz",1,1,&hse_fq);
	if (hse_fq && (RCC->OCRDYR & RCC_OCRDYR_HSERDY)==0) return -1;
	xprintf("in clk %d %X\n",hse_fq,root);
	
	RCC->RDLSICR = 1;	// LSI on
	RCC->DBGCFGR |= RCC_DBGCFGR_DBGCKEN;
	RCC->RCK12SELR = hse_fq ? 1:0;
	RCC->RCK3SELR = hse_fq ? 1:0;
	RCC->RCK4SELR = hse_fq ? 1:0;
	int i,div1=0,plls=0;
	uint32_t args[5];
	for (i=0;i<4;i++) {
		// pllN = <prediv mul P Q R>
		char buf[12] = "mp1,pll1"; buf[7] = '1'+i;
		if (fetch_fdt_ints(fdt,root,buf,5,5,args)<=0) continue;
		if (i<=1 && !div1) div1 = args[0];
		if (i<=1 && args[0] != div1) return -2; // div1/2 must be the same
		int sts = set_pll(i+1,hse_fq?hse_fq:64000,args[0],args[1],
				args[2],args[3],args[4]);
		if (sts<0) return sts;
		plls |= 1<<i;
	}
	if (fetch_fdt_ints(fdt,root,"mp1,apb-divs",5,5,args)>0) {
		for (i=0;i<5;i++) 
			args[i] = clamp_int_warn(args[i],0,3);
		RCC->APB1DIVR = args[0]; RCC->APB2DIVR = args[1];
		RCC->APB3DIVR = args[2]; RCC->APB4DIVR = args[3];
		RCC->APB5DIVR = args[4];
	}
	if (fetch_fdt_ints(fdt,root,"mp1,rtc-div",1,1,args)>0)
		RCC->RTCDIVR = clamp_int_warn(args[0],1,64)-1;
	if (plls & 1) RCC->MPCKSELR = 2;	// MPU from PLL
	if (plls & 2) RCC->ASSCKSELR = 2;	// AXI from PLL
	if (plls & 4) RCC->MSSCKSELR = 3;	// MCU from PLL3P
	if (hse_fq) stgen_setup(hse_fq,1,1);
	if (fetch_fdt_ints(fdt,root,"mp1,qspi-div",1,1,args)>0)
		qspi_set_divider(clamp_int_warn(args[0],1,64));
	return 0;
}

static int run_lse(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	check_lse();
	return 0;
}

static int run_rtc(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	check_rtc();
	return 0;
}

DECLARE_MOD(mp1_hse) {
	.name = "mp1,hse", .run = run_hse
};
DECLARE_MOD(mp1_csi_comp) {
	.name = "mp1,csicomp", .run = run_csi_comp
};
DECLARE_MOD(mp1_clocks) {
	.name = "mp1,clocks", .run = run_clocks
};
DECLARE_MOD(mp1_lse) {
	.name = "mp1,lse", .run = run_lse
};
DECLARE_MOD(mp1_rtc) {
	.name = "mp1,rtc", .run = run_rtc
};

static int run_gpio(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	uint32_t *arg;
	int i,sz = lookup_fdt(fdt,"mp1,mux",&arg,root);
	if (sz>=4) {
		sz /= 4;
		for (i=0;i<sz;i++,arg++) {
			uint32_t x = be_cpu(*arg);
			xprintf("setup gpio %X\n",x);
			gpio_setup_one(x & 0xff,x >> 16,(x >> 8) & 0x1f);
		}
	}
	sz = lookup_fdt(fdt,"mp1,wr32",&arg,root);
	if (sz >= 12) {
		sz /= 4;
		for (i=0;i<sz;i++) arg[i] = be_cpu(arg[i]);
		sz /= 3;
		for (i=0;i<sz;i++,arg+=3) {
			xprintf("setup mem %X %X %X\n",arg[0],arg[1],arg[2]);
			if (arg[0] & 3) continue; // bad address
			uint32_t *p = (uint32_t*)arg[0];
			if (arg[2]) *p = (*p & arg[2]) | arg[1];
			else *p = arg[1];
		}
	}
	return 0;
}
DECLARE_MOD(mp1_gpio) {
	.name = "mp1,gpio", .run = run_gpio
};

