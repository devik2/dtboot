#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "system.h"
#include "xprintf.h"
#include "coro.h"
#include "stm32mp157axx_cm4.h"
#include "fdt.h"
#include "uimage.h"
#include "mtd.h"
#include "ca7.h"
#include "gpio.h"

#define FTD_POS1 0x40000	// FDT blob pos at MTD
#define FTD_POS2 0x80000	// alt FDT blob pos at MTD
#define FDT_BUFA 0x10000000	// FDT buf addr before relocating to DDR
#define XTAL_MHZ 24

#define PIN_DDR_EN PM_Z(6)
#define PIN_DDR_STOP PM_A(3)
#define PIN_3V_STOP PM_H(11)
#define PIN_QCLK_EN PM_A(12)
#define PIN_GLED PM_A(13)

// rev.1 pins
#define PIN2_LED2 PM_H(5)
#define PIN2_QCLK_EN PM_Z(3)
#define PIN2_VBOOST PM_Z(0)
#define PIN2_ACK PM_F(14)
#define PIN2_SDA PM_Z(1)
#define PIN2_SCL PM_Z(4)

struct stm32mp1_mctx mp1_mctx;
const char git_version[] = GIT;

static void mp1_minimal_ddr_clock()
{
	RCC->RDLSICR = 1;	// LSI on
	RCC->DBGCFGR |= RCC_DBGCFGR_DBGCKEN;
	RCC->RCK12SELR = 0; // 0=HSI
	int r = set_pll(2,64000,5,47,3,0,2);  // AXI=200,GPU,DDR=300
	if (r<0) {
		xprintf("pll set error %d\n",r);
		asm("bkpt");
	}
}

static void rcc_to_defaults()
{
	// switch AXI, MPU, MCU to HSI before acting on PLLs
	RCC->MPCKSELR = 0; RCC->ASSCKSELR = 0; RCC->MSSCKSELR = 0;
	RCC->APB1DIVR = 1;
	RCC->APB2DIVR = 1;
	RCC->APB3DIVR = 1;
}

extern char _bss_start[];
extern char _bss_end[];

#if NSEC
int get_CPSR();
int get_SPSR();
int get_SP();
int get_SCR();
int get_ACTLR();
int get_SCTLR();
int get_VBAR();
int get_MVBAR();
extern int p_scr;
uint32_t psci_handler(uint32_t cmd,uint32_t a1,uint32_t a2,uint32_t a3);
uint32_t smc_handler(uint32_t cmd,uint32_t a1,uint32_t a2,uint32_t a3)
{
	xprintf("[%d] SMC %X %X %X %X %X pscr:%X\n",
			// for SMC_SET_FREQ time may be imprecise for moment
			cmd == SMC_SET_FREQ ? 0:get_ms_precise(),
			cmd,a1,a2,a3,get_SP(),p_scr);
	if (cmd == SMC_DUMP_INFO) {
		dump_reg("CPSR",get_CPSR(),_bn_cpsr);
		dump_reg("SPSR",get_SPSR(),_bn_cpsr);
		dump_reg("ACTLR",get_ACTLR(),_bn_actlr);
		dump_reg("SCTLR",get_SCTLR(),_bn_sctlr);
		dump_reg("SCR",p_scr,_bn_scr);
		xprintf("VBAR:%X MVBAR:%X\n",get_VBAR(),get_MVBAR());
		return 0;
	}
	if (cmd == SMC_SET_FREQ) {
		asm ("mcr p15, 0, %0, c14, c0, 0"::"r"(a1));
		return 0;
	}
	if ((cmd & 0xff000000) == 0x84000000)
		return psci_handler(cmd,a1,a2,a3);
	return -1;
}
#endif
//void prep_cpu2();

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
	mctx->fdt_ddr = (void*)hdr.ih_load;
		
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
	const gport_t outs[] = { PIN_DDR_EN, PIN_DDR_STOP, PIN_3V_STOP, 
			PIN_GLED, PIN2_LED2, 0 };
	gpio_setup_same(outs,PM_OUT|PM_DFLT(0),0);
	gpio_setup_one(PIN2_QCLK_EN, PM_OUT|PM_DFLT(1),0);
	// PIN_QCLK_EN from rev1 is USB FS port at rev2, use only pullup
	// in case we are on rev2
	// TODO: remove it as v1 is unsupported ?
	gpio_setup_one(PIN_QCLK_EN, PM_IN|PM_PU,0);
}

void linux_enter_hook()
{
	int nons = mp1_mctx.boot_flags[BFI_NONS];
	if (nons>0 && nons & 4) asm("bkpt");

	// turn red led off (boot done)
	gpio_setup_one(PIN2_LED2, PM_OUT|PM_DFLT(1),0);
}

// MP1 starts on HSI, prepare main console on some default
// pins. It will be replaced once HSE and DT is available
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

static void ddr_init_local(int fast)
{
	gpio_setup_one(PIN_DDR_EN,PM_OUT|PM_DFLT(1),0);
	udelay(10000);
	mctx->ddr_mb = ddr_init(0,1,fast); 
	xprintf("[%d] DDR done. (%X,%dMB)\n",get_ms_precise(),
			GPIOZ->ODR,mctx->ddr_mb);
}

extern uint32_t prog_x; // to detect prog mode
extern struct mtd_dev_t *prog_dev;

static void run_jtag_prog(struct mtd_dev_t *mtd)
{
	prog_dev = mtd;
	mp1_minimal_ddr_clock();
	ddr_init_local(1);
	qspi_set_divider(10);
	xprintf("MTD programming ready\n");
	console_sync();
	asm("bkpt");
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

static void call_secure_hook()
{
	if (hook_boot_secure) hook_boot_secure();
}

static uint32_t otp57,otp58;
static void read_secure_otp()
{
	RCC->MP_APB5ENSETR = RCC_MC_APB5ENSETR_BSECEN;
	(void)RCC->MP_APB5ENSETR;

	udelay(100);
	BSEC->BSEC_OTP_CONFIG = 15; // enable BSEC
	udelay(1000);
	while (BSEC->BSEC_OTP_STATUS & BSEC_OTP_STATUS_BUSY);
	BSEC->BSEC_OTP_CONTROL = 57; // read OTP data
	udelay(100);
	while (BSEC->BSEC_OTP_STATUS & BSEC_OTP_STATUS_BUSY);
	BSEC->BSEC_OTP_CONTROL = 58;
	udelay(100);
	while (BSEC->BSEC_OTP_STATUS & BSEC_OTP_STATUS_BUSY);

	otp57 = BSEC->BSEC_OTP_DATA[57]; // MAC address
	otp58 = BSEC->BSEC_OTP_DATA[58];
}

// we are entering in secure SVC mode
void main(const boot_api_context_t *ctx)
{
	RCC->MP_AHB4ENSETR = RCC_MC_AHB4ENSETR_GPIOAEN|RCC_MC_AHB4ENSETR_GPIOBEN|
		RCC_MC_AHB4ENSETR_GPIOCEN|RCC_MC_AHB4ENSETR_GPIODEN|
		RCC_MC_AHB4ENSETR_GPIOEEN|RCC_MC_AHB4ENSETR_GPIOFEN|
		RCC_MC_AHB4ENSETR_GPIOGEN|RCC_MC_AHB4ENSETR_GPIOHEN|
		RCC_MC_AHB4ENSETR_GPIOIEN;
	RCC->MP_AHB5ENSETR = RCC_MC_AHB5ENSETR_GPIOZEN;
	RCC->MP_APB3ENSETR = RCC_MC_APB3ENSETR_SYSCFGEN;
	RCC->MP_AHB6ENSETR = RCC_MC_AHB6ENSETR_CRC1EN;
	RCC->MP_APB5ENSETR = RCC_MC_APB5ENSETR_STGENEN;
	memset(_bss_start,0,_bss_end-_bss_start);

	mctx = &mp1_mctx.ctx;
	// ack supervisor MCU
	gpio_setup_one(PIN2_ACK,PM_OUT|PM_DFLT(0),0);

	// set by debugger or OS to modify boot behaviour
	mp1_read_boot_flags(&mp1_mctx);

	rcc_to_defaults();	// in case of soft reboots
	common_gpios_preinit();
	stgen_setup(64000,0,0);	// preliminary timer for delays
	coro_init();  // udelay becomes available
	try_boot_insn(1);
	call_secure_hook();

	set_tz_sec();
	stm32mp_init_gic(0);
	read_secure_otp();
	int nons = mp1_mctx.boot_flags[BFI_NONS];
	if (nons<0) nons = 0;
	// TODO: following check must be always true for authenticated
	// image (once implemented) to maintain security
	if (!(nons & 1) && !prog_x) {
		call_smc(SMC_NSEC,0,0); // switch to NS state
		setup_ns_mode();
	}

	mp1_initial_console();
	mp1_show_boot_flags();
	if (nons & 2 && !prog_x) asm("bkpt");

	//call_smc(SMC_DUMP_INFO,0,0);

	// init MTD to get FDT
	struct mtd_dev_t mtd;
	if (mtd_detect_qspi(&mtd,0,0)) 
		panic("can't init MTD\n");
	mctx->mtd = &mtd;
	
	xprintf("MTD: %s %s, VBAR=0x%X, CPU:%s\n",
		MTD_ISNAND(mtd.chip) ? "NAND":"NOR", mtd.chip->name, 
		get_VBAR(),mp1_get_rpn_name());

	if (prog_x) run_jtag_prog(&mtd);
	try_boot_insn(0);

	qspi_set_divider(7); // we are at 64MHz!

	int alt = mp1_mctx.boot_flags[BFI_DTB_ID]>0 ? 1:0;
	uint32_t *fdt_cob = prescan_fdt(&mtd,alt);

	psci_setup(mctx->fdt,fdt_cob);
	console_sync();

	// rest of init is controled by DT
	run_modules(mctx->fdt,fdt_cob,nons & 8);

	asm("bkpt");
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
	uint32_t ph = 0, fast = 0;
	fetch_fdt_ints(fdt,root,"patch-size-to",1,1,&ph);
	fetch_fdt_ints(fdt,root,"fast",1,1,&fast);
	ddr_init_local(fast);
	if (ph) do {
		uint32_t *p,*hdl = lookup_fdt_by_phandle(fdt,ph,NULL);
		if (!hdl) break;
		if (lookup_fdt(fdt,"reg",&p,hdl)!=8) break;
		xprintf("patching mem size\n");
		p[1] = be_cpu(mctx->ddr_mb << 20);
	} while(0);
	return 0;
}

DECLARE_MOD(mp1_ddr3) {
	.name = "mp1,ddr3", .run = run_ddr
};

static int stm8_read(int cmd,int sz,uint8_t *buf)
{
	uint8_t _cmd = cmd;
	int res = i2c_wr(I2C5,0x47,&_cmd,1,1);
	udelay(1000);
	if (res) return res;
	res = i2c_rd(I2C5,0x47,buf,sz);
	udelay(1000);
	return res;
}

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

static int run_stm8(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	RCC->MP_APB1ENSETR = RCC_MC_APB1ENSETR_I2C5EN;
	(void)RCC->MP_APB1ENSETR;

	gport_t i2cp[] = { PIN2_SCL,PIN2_SDA,0 };
	gpio_setup_same(i2cp,PM_ALT|PM_PU|PM_OD,4);
	udelay(1000);
	i2c_setup(I2C5);

	// try to read STM8 module info
	uint8_t buf[16]; int i,ok,r;
	for (i=0,ok=0;i<3;i++) {
		if (i) {
			udelay(8192*i);
			xprintf("stm8 connect retry %d\n",i);
		}
		memset(buf,0,sizeof(buf));
		r = stm8_read(0x5a,4,buf);
		if (r) r = stm8_read(0x5a,4,buf); // fast retry
		if (r) {
			xprintf("can't connect to STM8\n");
			continue;
		}
		if (strncmp(buf,"NORM",4)) {
			xprintf("bad response from 0x5A cmd: %02X %02X %02X %02X\n",
					buf[0],buf[1],buf[2],buf[3]);
			continue;
		}
		memset(buf,0,sizeof(buf));
		r = stm8_read(0x88,6,buf);
		if (r) {
			xprintf("can't read MAC from STM8\n");
			continue;
		}
		ok = 1;
		break;
	}
	if (!ok) return -1;
	for (i=0,r=0;i<6;i++) if (buf[i]) r++;
	if (!r) {
		xprintf("read zero MAC from STM8\n");
		return -2;
	}
	patch_mac_address(fdt,root,buf);
	return 0;
}

DECLARE_MOD(mp1_stm8) {
	.name = "mp1,stm8", .run = run_stm8
};

static int run_otp(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	uint8_t mac[6] = { 0x70,0xB3,0xD5,0xDC,0x10,0 };

	if (!otp57 && !otp58) return -1;
	if (!otp57) {
		// special hack to allocate our own MAC range
		// without messing with regular OTP bits - it
		// allows for other MAC later
		memcpy(mac+4,((uint8_t*)&otp58)+2,2);
	} else {
		memcpy(mac,(uint8_t*)&otp57,4);
		memcpy(mac+4,(uint8_t*)&otp58,2);
	}
	patch_mac_address(fdt,root,mac);
	return 0;
}

DECLARE_MOD(mp1_otp) {
	.name = "mp1,otp", .run = run_otp
};
