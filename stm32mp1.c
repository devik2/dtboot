#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "xprintf.h"
#include "coro.h"
#include "gpio.h"
#include "system.h"
#include "fdt.h"

#define S_GPIO_MODER(G,P,M) S_BITFLD(G->MODER,P*2,2,M);
#define S_GPIO_OSPEEDR(G,P,S) S_BITFLD(G->OSPEEDR,P*2,2,S);
// zapne ALT funkci N na pinu P portu G
#define S_GPIO_ALT(G,P,N) S_BITFLD(G->AFR[(P>>3)&1],(P&7)*4,4,N); \
			S_GPIO_MODER(G,P,2)
#if 0
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

static void csi_on()
{
	RCC->OCENSETR = RCC_OCENSETR_CSION;
	while (!(RCC->OCRDYR & RCC_OCRDYR_CSIRDY)) coro_yield();
	xprintf("CSI on\n");
}

static void compensation_on()
{
#if MPU_TYPE == 13
	xprintf("TODO: COMP skip because prob on MP13\n");
#else
	RCC->RCC_NNAME(,APB3ENSETR) = RCC_NNAME(RCC_,APB3ENSETR_SYSCFGEN);
	(void)RCC->RCC_NNAME(,APB3ENSETR);
	SYSCFG->CMPENSETR = 1;
	while (!(SYSCFG->CMPCR & SYSCFG_CMPCR_READY)) coro_yield();
	SYSCFG->CMPCR &= ~SYSCFG_CMPCR_SW_CTRL;
	xprintf("COMPensation on\n");
#endif
}
#endif

#define PIN_OSC_EN PM_Z(3)
// try to wait for external or xtal clock, returns 0 if ok
int hse_on(int ext)
{
	gpio_setup_one(PIN_OSC_EN,PM_OUT|PM_DFLT(1),0);
	uint32_t conf = RCC_OCENSETR_HSEON;
	if (ext) conf |= RCC_OCENSETR_HSEBYP|RCC_OCENSETR_HSEDIGBYP;
	RCC->OCENSETR = conf;
	uint32_t tm = get_ms();
	while (!(RCC->OCRDYR & RCC_OCRDYR_HSERDY)) {
		if (get_ms()>tm+20) {
			// revert ext clock settings
			RCC->OCENCLRR = conf;
			xprintf("HSE (ext=%d) can't start in %d ms\n",ext,get_ms()-tm);
			return -1;
		}
		coro_yield();
	}
	//RCC->CPERCKSELR = 2; // use HSE as per_ck TODO
	xprintf("HSE (ext=%d) on in %d ms\n",ext,get_ms()-tm);
	return 0;
}

static uint32_t fq1e3_cache,fq1e6_cache;
uint8_t ms_freq_sh;		// approx shift for ms cnv

uint32_t call_smc(uint32_t cmd,uint32_t a1,uint32_t a2);
void stgen_setup(int khz,int use_hse,int keep)
{
	// STGEN is already running, but flexgen should be glitch free
	rcc_flx_chan_set(33,use_hse ? 6:5,1,1);
	int hz = khz*1000;
	uint64_t x = 0;
	if (keep) { // assume CNTCVU is zero (valid for boot time<1min)
		x = STGENR->CNTCVL;
		x = x*hz/get_arm_tfreq(); // recompute time for new clock
	}
	call_smc(SMC_SET_STGEN,hz,x);

	for (ms_freq_sh = 0;hz;ms_freq_sh++) hz >>= 1;
	ms_freq_sh -= 10;
	fq1e6_cache = fq1e3_cache = 0;
	uint64_t t = get_arm_time();
	uint32_t tf = get_arm_tfreq();
	xprintf("[%d] delay tmr %X %X @%d\n",get_ms_precise(),
			(int)get_arm_time(),(int)(t>>32),tf);
}

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
	uint32_t f = get_arm_tfreq()>>20;
	uint64_t tgt = get_arm_time() + f*us;
	while (seq64_after(tgt,get_arm_time())) coro_yield();
}

// p & len must be 32B aligned, max len is 64k
static void crc_dma(const char *p,int len,int rpt)
{
#if 0
	MDMA_Channel1->CIFCR = 0x1f;	// clear all flags
	MDMA_Channel1->CSAR = (int)p;
	MDMA_Channel1->CDAR = (int)&CRC1->DR;
	MDMA_Channel1->CBNDTR = len|(rpt<<MDMA_CBNDTR_BRC_Pos);
	// use 64b 4beat source and 32b 8beat dst bursts
	// it can do 70MB/s at 120MHz AXI
	MDMA_Channel1->CTCR = ((32-1)<<MDMA_CTCR_TLEN_Pos)|
		MDMA_CTCR_DINCOS_1|MDMA_CTCR_SINCOS_1|
		MDMA_CTCR_PKE|MDMA_CTCR_SINCOS_0|MDMA_CTCR_SSIZE_0|
		MDMA_CTCR_DBURST_1|MDMA_CTCR_DBURST_0|
		MDMA_CTCR_SBURST_1|
		MDMA_CTCR_DSIZE_1|MDMA_CTCR_SSIZE_1|
		MDMA_CTCR_SINC_1|
		MDMA_CTCR_TRGM_1|MDMA_CTCR_SWRM;
	MDMA_Channel1->CCR = MDMA_CCR_EN;
	MDMA_Channel1->CCR = MDMA_CCR_EN|MDMA_CCR_SWRQ;
	while (!(MDMA_Channel1->CISR & MDMA_CISR_CTCIF)) coro_yield();
#endif
}
// fast HW checker
uint32_t crc32 (uint32_t crc, const void *p, uint32_t len)
{
	int dma = 0;
	//xprintf("CRC start %X %X %d\n",crc,(uint32_t)p,len);
	CRC->INIT = ~crc;
	CRC->CR = CRC_CR_RESET|CRC_CR_REV_OUT|CRC_CR_REV_IN_0|CRC_CR_REV_IN_1;
	if ((((int)p) & 31) == 0) { // 32B aligned
		if (len>=0x10000 && dma) {
			crc_dma(p,0x10000,(len>>16)-1);
			p += len & 0xfff0000;
			len &= 0xffff;
		}
		if (len<128 || !dma) {
			while (len>=4) {
				CRC->DR = *(int*)p;
				len-=4; p+=4;
			}
		} else {
			crc_dma(p,len & 0xffe0,0);
			p += len & 0xffe0;
			len &= 31;
		}
	}
	CRC->CR = CRC_CR_REV_OUT|CRC_CR_REV_IN_0;
	while (len>0) {
		*(volatile char*)&CRC->DR = *(const char*)p;
		len--; p++;
	}
	uint32_t co = ~CRC->DR;
	//xprintf("CRC res %X\n",co);
	return co;
}

// allow port Z and DDR be accessed from nonsec mode
static GPIO_TypeDef *gpio_get(uint16_t port);
void set_tz_sec() 
{
#if MPU_TYPE == 15
	RCC->MP_AHB5ENSETR = RCC_MC_AHB5ENSETR_GPIOZEN;
	(void)RCC->MP_AHB5ENSETR;	// dummy read
	GPIOZ->SECCFGR = 0;		// port Z as unsecure

	RCC->MP_APB5ENSETR = RCC_MC_APB5ENSETR_TZPCEN|RCC_MC_APB5ENSETR_TZC1EN|
		RCC_MC_APB5ENSETR_TZC2EN;
	(void)RCC->MP_APB5ENSETR;
	TZPC->DECPROT0 = 0x3ffffff;
	TZPC->TZMA1_SIZE = 0;
	TZC->REG_ID_ACCESSO =  0xffffffff;
	TZC->REG_ATTRIBUTESO = 0xc0000003;
	TZC->GATE_KEEPER = 3;
#elif MPU_TYPE == 13
	// mark GPIO A..I as non-secure
	for (int i=1;i<=9;i++) gpio_get(i<<4)->SECCFGR = 0;

	RCC->MP_APB5ENSETR = RCC_MP_APB5ENSETR_ETZPCEN|RCC_MP_APB5ENSETR_TZCEN;
	(void)RCC->MP_APB5ENSETR;
	ETZPC->DECPROT0 = 0xffffffff;
	ETZPC->DECPROT1 = 0xffffffff;
	ETZPC->DECPROT2 = 0xffffffff;
	ETZPC->DECPROT3 = 0xffffffff;
	ETZPC->TZMA1_SIZE = 0;	// all RAM is NS
	TZC->REG_ID_ACCESSO =  0xffffffff;
	TZC->REG_ATTRIBUTESO = 0xc0000003;
	TZC->GATE_KEEPER = 3;
#endif
}

static int mp1_check_flags(const uint8_t *flg)
{
	uint8_t sum = 0;
	for (;*flg & 0xf0;flg++) sum ^= *flg;
	sum ^= sum>>4;
	return (*flg & 0xf) == (sum & 0xf);
}

static int mp1_apply_flags(int8_t *tab,const uint8_t *flg)
{
	if (!mp1_check_flags(flg)) return 0;
	for (;*flg & 0xf0;flg++) {
		uint8_t n = *flg>>4;
		if (n<=2) memset(tab+1,0xff,2); // flg 1&2 are paired
		tab[(*flg>>4) & 0xf] = *flg & 0xf;
	}
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
#if 0
	PWR->CR1 |= PWR_CR1_DBP; 
	volatile uint32_t *t = &TAMP->BKP0R;
	t[BOOT_MODE_INSN] = 0x11770000|insn;
	// TODO
#endif
}

#if MPU_TYPE == 15
// code to save port Z (I2C) to RAM using DMA
void setup_dma_sniffer()
{
	uint32_t mem = 0x10040000; // SRAM3
	memset((void*)mem,0,0x10000);
	RCC->MP_APB1ENSETR = RCC_MC_APB1ENSETR_TIM6EN;
	RCC->MP_AHB2ENSETR = RCC_MC_AHB2ENSETR_DMA1EN|
			RCC_MC_AHB2ENSETR_DMAMUXEN;
	TIM6->ARR = 64; // 1MHz update
	TIM6->DIER = TIM_DIER_UDE;
	TIM6->CR1 = TIM_CR1_CEN;
	DMAMUX1_Channel0->CCR = 69; // TIM6UP
	DMA1->LIFCR = 0xfffffff;
	DMA1_Stream0->NDTR = 0xffff;
	DMA1_Stream0->PAR = (uint32_t)&GPIOZ->IDR;
	DMA1_Stream0->M0AR = mem|0x20000000;
	DMA1_Stream0->CR = DMA_SxCR_MINC|DMA_SxCR_MSIZE_1;
	DMA1_Stream0->FCR = DMA_SxFCR_DMDIS;
	DMA1_Stream0->CR |= DMA_SxCR_EN;
	xprintf("sniff dma CR %x %x %x\n",DMA1_Stream0->CR,
			DMA1_Stream0->M0AR,DMA1->LISR);
}
#endif

void mp1_show_boot_flags()
{
	struct stm32mp1_mctx *ctx = (struct stm32mp1_mctx *)mctx;
	char buf[80]="bflags:",*p=buf+7;
	int i;
	for (i=1;i<16;i++) {
		if (ctx->boot_flags[i]<0) continue;
		p += xsnprintf(p,5," %x=%x",i,ctx->boot_flags[i]);
	}
	xprintf("INSN:%d %s; DENABLE=0x%X\n",ctx->boot_insn,buf,BSEC->DENR);
}

void i2c_setup(I2C_TypeDef *I2C)
{
	I2C->CR1 = 0;
	I2C->TIMINGR = 0x7077c3c7;
	I2C->CR1 = I2C_CR1_PE;
}

// aut is auto-end, use when no trailing read is needed
int i2c_wr(I2C_TypeDef *I2C,int addr,const char *data,int len,int aut)
{
	I2C->ISR = I2C_ISR_TXE; // clear TX register
	I2C->ICR = I2C_ICR_STOPCF|I2C_ICR_NACKCF;
	I2C->CR2 = (addr<<1)|I2C_CR2_START|(aut?I2C_CR2_AUTOEND:0)|(len<<16);
	int i,tmo;
	for (i=0;i<len;i++) {
		tmo = 100000;
		while (!(I2C->ISR & I2C_ISR_TXIS)) {
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

struct mp2_rpns_t {
	uint32_t code;
	char name[6];
} static const mp25_rpns[] = {
	{ 0x00082182,"235C" }, { 0x000B318E,"233C" }, { 0x000B31EF,"231C" }, { 0x40082F82,"235A" },
	{ 0x400B3F8E,"233A" }, { 0x400B3FEF,"231A" }, { 0x80082182,"235F" }, { 0x800B318E,"233F" },
	{ 0x800B31EF,"231F" }, { 0xC0082F82,"235D" }, { 0xC00B3F8E,"233D" }, { 0xC00B3FEF,"231D" },
	{ 0x00002000,"257C" }, { 0x00082000,"255C" }, { 0x000B2004,"253C" }, { 0x000B3065,"251C" },
	{ 0x40002E00,"257A" }, { 0x40082E00,"255A" }, { 0x400B2E04,"253A" }, { 0x400B3E65,"251A" },
	{ 0x80002000,"257F" }, { 0x80082000,"255F" }, { 0x800B2004,"253F" }, { 0x800B3065,"251F" },
	{ 0xC0002E00,"257D" }, { 0xC0082E00,"255D" }, { 0xC00B2E04,"253D" }, { 0xC00B3E65,"251D" },
	{ 0,"" }
};

uint32_t dbg_idc,rpn,cpu_id[3];
static const char *rpnn_cache;
const char *mp1_get_rpn_name()
{
	if (rpnn_cache) return rpnn_cache;
	uint32_t idc = 0xffff & dbg_idc;
	const struct mp2_rpns_t *tab = NULL;
	switch (idc) {
		case 0x505: tab = mp25_rpns; break;
	}
	if (!tab) return "UNKNWN";
	//uint32_t rpn = *(uint32_t*)0x44000024; LOADED BY SEC
	int i;
	rpnn_cache = "UNKNOWN";
	for (i=0;tab[i].name[0];i++) 
		if (tab[i].code == rpn) rpnn_cache = tab[i].name;
	return rpnn_cache;
}

// GPIO support
static GPIO_TypeDef *gpio_get(uint16_t port)
{
	if ((port & 0xf0) == 0xf0) 
		return (GPIO_TypeDef*)GPIOZ_BASE;
	return (GPIO_TypeDef*)(GPIOA_BASE+(GPIOB_BASE-GPIOA_BASE)*((port>>4)-1));
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
	if ((mode & 3) == PM_IN || mode & PM_ALT || mode & PM_OD) {
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

// returns 0,1 (hard strapped) or 2 (floating)
__attribute__((noinline)) uint32_t gpio_pin_state(gport_t p)
{
	gpio_setup_one(p,PM_IN|PM_PU,0);
	udelay(10);
	int a = gpio_in(p);
	gpio_setup_one(p,PM_IN|PM_PD,0);
	udelay(10);
	int b = gpio_in(p);
	if (a != b) return 2;
	return a;
}

static int run_hse(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	struct stm32mp1_mctx *ctx = (struct stm32mp1_mctx *)mctx;

	if (hse_on(1)) { 
		xprintf("Can start no HSE\n");
		return -1;
	}
	xprintf("HSE started, board_rev detected: %d\n",ctx->lump_rev);
	return 0;
}
#if 0
static int run_csi_comp(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	csi_on();
	compensation_on();
	return 0;
}
#endif

struct PLL_TypeDef 
{
  __IO uint32_t CFGR1;
  __IO uint32_t CFGR2;
  __IO uint32_t CFGR3;
  __IO uint32_t CFGR4;
  __IO uint32_t CFGR5;
       uint32_t RESERVED;
  __IO uint32_t CFGR6;
  __IO uint32_t CFGR7;
};
static uint32_t pll_2_8[7] = { 0x590,0x5b8,0x1360,0x1388,0x13b0,0x13d8,0x1400 };
static uint8_t pll_muxes[8] = { 5,6,7,0,1,2,3,4 };

int32_t rcc_set_pll1(uint32_t src,uint32_t m,uint32_t n,uint32_t d1,uint32_t d2)
{
	if (src>2 || m<1 || m>63 || n<20 || n>320 || d1<1 || d1>7 || d2<1 || d2>7) return -1;
	CA35SSC->CHGCLKREQ_WS1 = 1; // switch to exclk (flx 63, typ. HSI)
	int tmo = 0;
	while (!(CA35SSC->CHGCLKREQ_RW & 2)) {
		coro_yield();
		if (++tmo > 1000000) {
			_halt();
			return -2;
		}
	}
	RCC->MUXSELCFGR = (RCC->MUXSELCFGR & 0xff0fffff)|(src << 20);
	CA35SSC->PLL_FREQ1_RW = n|(m<<16);
	CA35SSC->PLL_FREQ2_RW = d1|(d2<<3);
	CA35SSC->PLL_ENABLE_WS1 = 0x5;
	CA35SSC->BRM_WS1 = 0x10001; // slow clk 32x when in IDLE
	tmo = 0;
	while (!(CA35SSC->PLL_ENABLE_RW & 2)) {
		coro_yield();
		if (++tmo > 1000000) {
			_halt();
			return -3;
		}
	}
	CA35SSC->CHGCLKREQ_WC1 = 1; // switch to PLL
	return 0;

}

// m is 1..63, n is 20..320
int32_t rcc_set_pll(uint32_t plln,uint32_t src,uint32_t m,uint32_t n,uint32_t d1,uint32_t d2)
{
	if (plln<2 || plln>8 || src>2 || m<1 || m>63 || n<20 || n>320 || d1<1 || d1>7 || d2<1 || d2>7) return -1;
	struct PLL_TypeDef *PLL = (struct PLL_TypeDef*)(RCC_BASE+pll_2_8[plln-2]);
	PLL->CFGR1 = 0;

	int sh = 4*pll_muxes[plln-1];
	RCC->MUXSELCFGR = (RCC->MUXSELCFGR & ~(3<<sh))|(src << sh);
	PLL->CFGR2 = (n << RCC_PLL4CFGR2_FBDIV_Pos)|m;
	PLL->CFGR4 = RCC_PLL4CFGR4_FOUTPOSTDIVEN;
	PLL->CFGR6 = d1;
	PLL->CFGR7 = d2;
	PLL->CFGR1 = RCC_PLL4CFGR1_SSMODRST|RCC_PLL4CFGR1_PLLEN;
	int tmo = 0;
	while (!(PLL->CFGR1 & RCC_PLL4CFGR1_PLLRDY)) {
		coro_yield();
		if (++tmo > 1000000) return -2;
	}
	return 0;
}


int rcc_flx_chan_set(unsigned ch,unsigned src,unsigned div,unsigned enable);
void qspi_set_divider(int div);
static int run_clocks(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	uint32_t hse_fq = 0; // 0 means HSI
	fetch_fdt_ints(fdt,root,"mp2,hse-khz",1,1,&hse_fq);
	if (hse_fq && (RCC->OCRDYR & RCC_OCRDYR_HSERDY)==0) return -1;
	
	int i,plls=0;
	uint32_t args[5];
	for (i=0;i<8;i++) {
		// pllN = <src prediv mul d1 d2> | src:0=hsi,1=hse,2=msi
		char buf[12] = "mp2,pll1"; buf[7] = '1'+i;
		if (fetch_fdt_ints(fdt,root,buf,5,5,args)<=0) continue;
		int32_t sts;
		if (i) sts = rcc_set_pll(i+1,args[0],args[1],args[2],args[3],args[4]);
		else sts = rcc_set_pll1(args[0],args[1],args[2],args[3],args[4]);
		if (sts<0) return sts;
		plls |= 1<<i;
	}
	for (i=0;i<64;i++) {
		// flxNN = <src div> | src:0..4=PLL4..8,5=hsi,6=hse,7=msi,D=lsi,E=lse
		char buf[12]; xsnprintf(buf,10,"mp2,flx%02d",i);
		if (fetch_fdt_ints(fdt,root,buf,2,2,args)<=0) continue;
		int32_t sts = rcc_flx_chan_set(i,args[0],args[1],1);
		if (sts<0) return sts;
	}
	if (fetch_fdt_ints(fdt,root,"mp2,apb-divs",5,5,args)>0) {
		for (i=0;i<5;i++) 
			args[i] = clamp_int_warn(args[i],0,4);
		RCC->APB1DIVR = args[0]; RCC->APB2DIVR = args[1];
		RCC->APB3DIVR = args[2]; RCC->APB4DIVR = args[3];
		RCC->APBDBGDIVR = args[4];
	}
	if (fetch_fdt_ints(fdt,root,"mp2,rtc-div",1,1,args)>0)
		RCC->RTCDIVR = clamp_int_warn(args[0],1,64)-1;
	if (fetch_fdt_ints(fdt,root,"mp2,lsm-div",1,1,args)>0)
		RCC->LSMCUDIVR = clamp_int_warn(args[0],1,2)-1;
	if (hse_fq) stgen_setup(hse_fq,1,1);
	return 0;
}
#if 0
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

DECLARE_MOD(mp1_lse) {
	.name = "mp1,lse", .run = run_lse
};
DECLARE_MOD(mp1_rtc) {
	.name = "mp1,rtc", .run = run_rtc
};
DECLARE_MOD(mp1_csi_comp) {
	.name = "mp1,csicomp", .run = run_csi_comp
};
#endif
DECLARE_MOD(mp1_hse) {
	.name = "mp1,hse", .run = run_hse
};
DECLARE_MOD(mp1_clocks) {
	.name = "mp2,clocks", .run = run_clocks
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
			coro_yield();
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
			uint32_t *p = (uint32_t*)(uint64_t)arg[0];
			if (arg[2]) *p = (*p & arg[2]) | arg[1];
			else *p = arg[1];
			coro_yield();
		}
	}
	return 0;
}
DECLARE_MOD(mp1_gpio) {
	.name = "mp1,gpio", .run = run_gpio
};

