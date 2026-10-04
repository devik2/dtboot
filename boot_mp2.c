#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "system.h"
#include "xprintf.h"
#include "coro.h"
#include "fdt.h"
#include "uimage.h"
#include "mtd.h"
#include "ca7.h"
#include "gpio.h"
#include "minilzo.h"
#include "ddr_mp2/stm32mp2xx_ll_rcc.h"
#include "stm32mp2xx_hal_ddr.h"
#include "stm32mp2xx_hal_conf_includes.h"

#define FTD_POS1 0x40000	// FDT blob pos at MTD
#define FTD_POS2 0x80000	// alt FDT blob pos at MTD
#define FDT_BUFA 0x0E080000	// FDT buf addr before relocating to DDR
#define XTAL_MHZ 40

#define PIN_DDR_EN PM_I(10)

#define PIN_GLED PM_H(4)
#define PIN_RLED PM_G(11)

struct stm32mp1_mctx mp1_mctx;
const char git_version[] = GIT;

DDR_InitTypeDef ddf;

int hse_on(int ext);
static void mp1_minimal_ddr_clock()
{
	hse_on(1);
#if 0
	RCC->RDLSICR = 1;	// LSI on
	RCC->DBGCFGR |= RCC_DBGCFGR_DBGCKEN;
	RCC->RCK12SELR = 0; // 0=HSI
	int r = set_pll(2,64000,5,47,3,0,2);  // AXI=200,GPU,DDR=300
	if (r<0) {
		xprintf("pll set error %d\n",r);
		_halt();
	}
#endif
}

static int rcc_flx_chan_sts(volatile uint32_t *base,unsigned ch)
{
	if (ch > 31) base++;
	return ((*base) >> (ch & 31)) & 1;
}

int rcc_flx_chan_set(unsigned ch,unsigned src,unsigned div,unsigned enable)
{
	ch &= 63;
	int cdiv = ((RCC->FINDIVxCFGR[ch] & RCC_FINDIVxCFGR_FINDIVx_Msk)+1)
		* (RCC->PREDIVxCFGR[ch]+1);
	if (src>14) return -1;
	if (cdiv == div && (RCC->XBARxCFGR[ch] & RCC_XBARxCFGR_XBARxSEL_Msk)==src)
		return 0; // no change

	int ndiv = div;
	// PREDIV can be 1,2,4,1024; let's keep it at highest multiple to save power
	unsigned prediv = 0;
	if (!div) return -1;
	if (!(div & 0x3ff)) {
		prediv = 0x3ff; div >>= 10;
	}
	else if (!(div & 1)) {
		prediv = 1; div >>= 1;
		if (!(div & 1)) { prediv = 3; div >>= 1; }
	}
	if (div > 64 || div < 1) return -1;
	if (ndiv < cdiv) { // we now use higher DIV, switch src first
		RCC->XBARxCFGR[ch] = RCC_XBARxCFGR_XBARxEN|src;
		while (RCC->XBARxCFGR[ch] & RCC_XBARxCFGR_XBARxSTS);
	}
	if (enable) enable = RCC_FINDIVxCFGR_FINDIVxEN;
	RCC->FINDIVxCFGR[ch] = (RCC->FINDIVxCFGR[ch] & RCC_FINDIVxCFGR_FINDIVxEN)|(div-1)|enable;
	while (rcc_flx_chan_sts(&RCC->FINDIVSR1,ch));
	RCC->PREDIVxCFGR[ch] = prediv;
	while (rcc_flx_chan_sts(&RCC->PREDIVSR1,ch));
	if (ndiv >= cdiv) { // we just set higher DIV, switch src now
		RCC->XBARxCFGR[ch] = RCC_XBARxCFGR_XBARxEN|src;
		while (RCC->XBARxCFGR[ch] & RCC_XBARxCFGR_XBARxSTS);
	}
	return 0;
}

static void rcc_to_defaults()
{
	// all flxgen emit HSI at start
	for (int i=0;i<64;i++) rcc_flx_chan_set(i,LL_RCC_XBAR_CLKSRC_HSI,1,0);

	RCC->LSMCUDIVR = 0;
	RCC->APB1DIVR = RCC->APB2DIVR = RCC->APB3DIVR = RCC->APB4DIVR = 0;
	RCC->APBDBGDIVR = 0;
}


__attribute__((noinline))
static uint32_t *prescan_fdt(struct mtd_dev_t *mtd,int alt)
{
	// load fdt from flash
	void *buf = (void*)FDT_BUFA;
	image_header_t hdr;
	if (prep_uimage(mtd,alt ? FTD_POS2:FTD_POS1,&hdr,PUF_CRC,buf)==-1) {
		panic("can't read FDT%d!\n",alt);
	}
	mctx->fdt = buf;
	mctx->fdt_size = hdr.ih_size; 
	mctx->fdt_ddr = (void*)(uint64_t)hdr.ih_load;
		
	uint32_t *ptr;
	if (lookup_fdt(mctx->fdt,"model",&ptr,NULL)>0)
		xprintf("FDT%d model: %s\n",alt,ptr);
	uint32_t *cob = lookup_fdt_section(mctx->fdt,"/dt-boot",NULL);
	if (!cob) panic("Missing /dt-boot in DTB");
	return cob;
}

// can be called for both LUMP rev1 and 2
static void common_gpios_preinit()
{
#if MPU_TYPE == 15
#warning TODO:enable for lump
#if 0
	const gport_t outs[] = { PIN_DDR_EN, PIN_DDR_STOP, PIN_3V_STOP, 
			PIN_GLED, PIN2_LED2, 0 };
	gpio_setup_same(outs,PM_OUT|PM_DFLT(0),0);
	gpio_setup_one(PIN2_QCLK_EN, PM_OUT|PM_DFLT(1),0);
	// PIN_QCLK_EN from rev1 is USB FS port at rev2, use only pullup
	// in case we are on rev2
	// TODO: remove it as v1 is unsupported ?
	gpio_setup_one(PIN_QCLK_EN, PM_IN|PM_PU,0);
#endif
#elif MPU_TYPE == 13
	const gport_t outs[] = { PIN3_GLED, PIN3_RLED, 0 };
	gpio_setup_same(outs,PM_OUT|PM_DFLT(0),0);
#endif
}

__attribute__ ((weak)) void linux_mach_hook() { }

void linux_enter_hook()
{
	int nons = mp1_mctx.boot_flags[BFI_NONS];
	if (nons>0 && nons & 4) _halt();

#if MPU_TYPE == 15
	// turn red led off (boot done)
	gpio_setup_one(PIN2_LED2, PM_OUT|PM_DFLT(1),0);
#endif
	linux_mach_hook();
}

extern uint32_t cpu_id[3];
static void boot_flags_quirks()
{
	if (cpu_id[0]==0x2E0041 && cpu_id[1]==0x41365007 && cpu_id[2]==0x353653) {
		mp1_mctx.boot_flags[BFI_UARTL] = 1;
		gpio_setup_one(PM_I(5),PM_OUT|PM_DFLT(1),0);
	}
}

// MP1 starts on HSI, prepare main console on some default
// pins. It will be replaced once HSE and DT is available
__attribute__((noinline))
static void mp1_initial_console()
{
	const int div = 64*1000000/115200; // divide HSI osc

	int uid = mp1_mctx.boot_flags[BFI_UARTL];
	if (uid<0) uid = mp1_mctx.boot_flags[BFI_UARTH]+16;
	int uart = mp1_uart_tx_setup(uid,1);
	if (!uart) return; // no console

	USART_TypeDef *u = mp1_get_uart(uart,0);
	if (!u) return;
	init_usart(0,u,div,0); // initial is non-buffered always
	xprintf("[%d] UART%d is set up, git_rev=%s\n",
			get_ms_precise(),uart,git_version);
}

int HAL_RCC_GetFreq(int type)
{
	_halt();
	return 0;
}

void HAL_RCC_MCOConfig(uint32_t RCC_MCOx, uint32_t RCC_MCOSource, uint32_t RCC_MCODiv)
{
	assert_param(0);
}

void assert_failed(uint8_t* file, uint32_t line)
{
	xprintf("assert_failed %s:%d\n",file,line);
}

uint32_t SystemA35_ManageTick(uint32_t x)
{
	assert_param(0);
	return 0;
}

void SystemCoreClockUpdate()
{
}

uint32_t HAL_GetTick()
{
	return get_ms();
}
  
DDR_InitTypeDef ddf;
uint32_t SystemCoreClock;
__attribute__ ((weak)) int ddr_init(uint32_t type)
{
	SystemCoreClock = 64000000;
	HAL_StatusTypeDef sts = HAL_DDR_Init(&ddf);
	xprintf("ddr %d\n",sts);
	if (sts) return 0;
	volatile uint32_t *mem = (void*)0x80000000;
	int j,mb;
	for (int i=6;i<10;i++) { // address shift
		mb = 1<<i;
		for (j=0;j<mb;j++) mem[256*1024*j] = j;
		int err = 0;
		for (j=0;j<mb;j++) err += mem[256*1024*j] != j;
		//xprintf("%d %d %d\n",i,mb,err);
		if (err) return mb/2;
	}
	return mb;
}

static void ddr_init_local(uint32_t ddr_type)
{
	gpio_setup_one(PIN_DDR_EN,PM_OUT|PM_DFLT(1),0);
	udelay(10000);
	mctx->ddr_mb = ddr_init(ddr_type); 
	xprintf("[%d] DDR done. (%dMB)\n",get_ms_precise(),
			mctx->ddr_mb);
}

extern uint32_t prog_x; // to detect prog mode
extern struct mtd_dev_t *prog_dev;

static void run_jtag_prog(struct mtd_dev_t *mtd,int alt_ddr)
{
	prog_dev = mtd;
	mp1_minimal_ddr_clock();
	ddr_init_local(alt_ddr ? DDRT_3L_32 : DDRT_3L_16);
	qspi_set_divider(2);
	xprintf("MTD programming ready\n");
	console_sync();
	_halt();
}

void stm32mp_init_gic(int cpu2);
void setup_ns_mode();
void psci_setup(struct boot_param_header *fdt,uint32_t *root);

__attribute__ ((weak)) int hook_boot_insn(uint8_t insn,int is_secure)
{
	return 0; // not processed
}

static void try_boot_insn(int secure)
{
	if (!mp1_mctx.boot_insn) return;
	if (!hook_boot_insn(mp1_mctx.boot_insn,secure)) return;

	// mark as done
	mp1_set_boot_insn(0);
	mp1_mctx.boot_insn = 0;
}

__attribute__ ((weak)) void hook_boot_secure()
{
}

uint8_t cached_mac_addr[2][6]; // filled from EL3 by cache_mac_address()

void test_thread(void *a)
{
	for (;;) {
	udelay(100000);
	xprintf("TEST2\n");
	}
}

static void test_eth_sw()
{
	// we need ker_ethsw 125MHz 
	// and ker_ethswref 50MHz (only if we are RMII clocker),
	RCC->ETHSWCFGR = RCC_ETHSWCFGR_ETHSWEN|RCC_ETHSWCFGR_ETHSWMACEN; // 0x54200818 <= 0x66
	(void)RCC->ETHSWCFGR;
	SYSCFG->ETH1CR = 0x13; // RGMII, internal clk; 0x54233000
	SYSCFG->ETHSWCR = 4; // RMII, internal 125MHz, external refclk; 0x54233800
	// ETHSW DEIP at 0x5c000000
	volatile uint32_t *deip = (void*)0x5c000000;
#define FESn_PORT_STATER(N) (0x200000+N*0x10000)
	deip[FESn_PORT_STATER(0)/4] = 0x120; // GMII
	deip[FESn_PORT_STATER(2)/4] = 0x200; // MII
#define FES_RMII_STS 0x1000402
	deip[FES_RMII_STS/4] = 1;
}

void mmu_on();
void main_el1()
{
	rcc_en_clock(&RCC->CRCCFGR);
	rcc_en_clock(&RCC->LPUART1CFGR);

	mctx = &mp1_mctx.ctx;
	// set by debugger or OS to modify boot behaviour
	mp1_read_boot_flags(&mp1_mctx);
	boot_flags_quirks();

	rcc_to_defaults();	// in case of soft reboots
	common_gpios_preinit();
	stgen_setup(64000,0,0);	// preliminary timer for delays
	coro_init();  // udelay becomes available
	//try_boot_insn(1); TODO

	int nons = mp1_mctx.boot_flags[BFI_NONS];

	if (nons<0) nons = 0;
#if 0
	// TODO: following check must be always true for authenticated
	// image (once implemented) to maintain security
	if (!(nons & 1) && !prog_x) {
		call_smc(SMC_NSEC,0,0); // switch to NS state
		setup_ns_mode();
	}

	if (nons & 2 && !prog_x) {
		//BSEC->BSEC_DENABLE = 0x47f; TODO
		for (volatile int i=0;i<1000;i++);
		_halt();
	}
#endif
	mp1_initial_console();
	mp1_show_boot_flags();
	//_halt();
#if 0
	coro_new(test_thread,0,0,"test1");
	for (;;) {
		udelay(100000);
		xprintf("TEST1\n");
	}
#endif
	if (nons & 2 && !prog_x) {
		//BSEC->BSEC_DENABLE = 0x47f; TODO
		_halt();
	}
	call_smc(SMC_DUMP_INFO,0,0);

	// init MTD to get FDT
	struct mtd_dev_t mtd;
	if (mtd_detect_qspi(&mtd,0,0)) 
		panic("can't init MTD\n");
	mctx->mtd = &mtd;
	
	int lzo = lzo_init();
	xprintf("MTD: %s %s, LZO=0x%X, CPU:%s progx:0x%X\n",
		MTD_ISNAND(mtd.chip) ? "NAND":"NOR", mtd.chip->name, 
		lzo,mp1_get_rpn_name(),prog_x);

	if (prog_x) run_jtag_prog(&mtd,nons & 8);
	try_boot_insn(0);
	mmu_on();

	qspi_set_divider(2); // we are at 64MHz!

	int alt = mp1_mctx.boot_flags[BFI_DTB_ID]>0 ? 1:0;
	uint32_t *fdt_cob = prescan_fdt(&mtd,alt);

	console_sync();

	// rest of init is controled by DT
	run_modules(mctx->fdt,fdt_cob,nons & 8);

	console_sync();
	_halt();
	for (;;) __WFI();
}
	
void plat_patch_linux_dtb(struct boot_param_header *fdt,uint32_t *lnx)
{
	uint32_t *ptr,*root = lookup_fdt_section(fdt,"/dt-boot",NULL);
	if (!root) return;

	// edit some changes to top section for use by linux
	if (lookup_fdt(fdt,"boot-flags",&ptr,root) == 16) 
		memcpy(ptr,mp1_mctx.boot_flags,16);
	if (lookup_fdt(fdt,"enter-shell",&ptr,root) == 4) 
		*ptr = be_cpu(mp1_mctx.boot_flags[BFI_SHELL]);
}

static int run_ddr(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	uint32_t ph = 0, type = DDRT_3L_16|DDRF_FTEST;
	fetch_fdt_ints(fdt,root,"patch-size-to",1,1,&ph);
	fetch_fdt_ints(fdt,root,"ddr-type",1,1,&type);
	ddr_init_local(type);
	if (ph) do {
		uint32_t *p,*hdl = lookup_fdt_by_phandle(fdt,ph,NULL);
		if (!hdl) break;
		if (lookup_fdt(fdt,"reg",&p,hdl)!=16) break;
		xprintf("patching mem size\n");
		p[3] = be_cpu(mctx->ddr_mb << 20);
	} while(0);
	return 0;
}

DECLARE_MOD(mp1_ddr3) {
	.name = "mp1,ddr3", .run = run_ddr
};

static int patch_mac_address(struct boot_param_header *fdt,
		uint32_t *root,const uint8_t *buf)
{
	uint32_t ph = 0;
	fetch_fdt_ints(fdt,root,"patch-mac-to",1,1,&ph);
	if (!ph) return -1;

	uint32_t *p,*hdl = lookup_fdt_by_phandle(fdt,ph,NULL);
	if (!hdl) return -2;
	if (lookup_fdt(fdt,"local-mac-address",&p,hdl)!=6) return -3;
	xprintf("patching MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
		buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
	memcpy(p,buf,6);
	return 0;
}

static int mac_valid(const uint8_t *m)
{
	static const uint8_t zero[6], ff[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
	return memcmp(m, zero, 6) && memcmp(m, ff, 6);
}

static int run_otp(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	for (int i = 0; i < 2; i++) {
		if (!mac_valid(cached_mac_addr[i])) continue;
		patch_mac_address(fdt, root, cached_mac_addr[i]);
		return 0;
	}
	return -1;
}

DECLARE_MOD(mp1_otp) {
	.name = "mp1,otp", .run = run_otp
};
