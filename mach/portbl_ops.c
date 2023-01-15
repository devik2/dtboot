#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "stm32mp1_ltdc.h"
#include "xprintf.h"
#include "coro.h"
#include "gpio.h"
#include "system.h"
#include "fdt.h"
#include "smi.h"
#include "Include/stm32mp157axx_cm4.h"

// Usage of BFI_MACH1:
// standby control; b0=1: autowakeup in 2 sec, b1=1:keep dbg in standby

#define PINn_18_DISP PM_B(9)
#define PINn_50V_ON PM_H(13)
#define PINr_50V_ON PM_I(4)
#define PIN_RUN_ON PM_G(12)
#define PINn_SX_RST PM_G(13)
#define PIN_ETH_RST PM_B(15)
#define PINn_CK32 PM_C(6)
#define PINo_EMMC_ON PM_D(5)
#define PINo_EMMCIO_ON PM_A(15)

#define FB_ADDR 0x2ffe0000	// palleted FB for logo & live data

static void dbg()
{
#define PIN_SCL PM_D(7)
	gpio_setup_one(PIN_SCL,PM_OUT|PM_DFLT(0),0);
	udelay(1000);
	gpio_out(PIN_SCL,1);
	udelay(1000);
	gpio_out(PIN_SCL,0);
	udelay(1000);
	gpio_setup_one(PIN_SCL,PM_ALT,4);
	udelay(1000);
}

// returns 0/1 or 2 for no-connected
static __attribute__((noinline)) uint32_t detect_pin_conn(gport_t pin)
{
	gpio_setup_one(pin,PM_IN|PM_PU,0);
	udelay(100);
	int u = gpio_in(pin);
	gpio_setup_one(pin,PM_IN|PM_PD,0);
	udelay(100);
	int d = gpio_in(pin);
	gpio_setup_one(pin,PM_AN,0);
	if (u == d) return u;
	return 2;
}

// as detect_pin_conn, but returns 3 if pin connected to pin2 driver
static __attribute__((noinline)) uint32_t detect_pin_conn2(
		gport_t pin,gport_t pin2)
{
	uint32_t r = detect_pin_conn(pin);
	if (r<2) return r;
	gpio_setup_one(pin2,PM_OUT|PM_DFLT(0),0);
	r = detect_pin_conn(pin);
	gpio_setup_one(pin2,PM_AN,0);
	return r ? r : 3;
}

static char brd_rev;	// portable brd rev

#define PIN_RV0 PM_E(13)
#define PIN_RV1 PM_D(10)
static void detect_rev()
{
	uint32_t st0,st1;
	char rtab[16]="..RO" "..S." "P.N." "....";
	st0 = detect_pin_conn2(PIN_RV0,PIN_RV1);
	st1 = detect_pin_conn(PIN_RV1);
	st0 |= st1<<2;
	brd_rev = rtab[st0];
	if (brd_rev == '.') brd_rev = 'N';
	xprintf("detected board rev %c (%d)\n",brd_rev,st0);
}

static void patch_board_rev(struct boot_param_header *fdt)
{
	uint32_t *ptr,*root = lookup_fdt_section(fdt,"/dt-boot",NULL);
	if (!root) return;

	if (lookup_fdt(fdt,"board-rev",&ptr,root) >= 1) 
		*(char*)ptr = brd_rev;
}

static int sx_ok;
static int sx_check_presence()
{
	RCC->MP_APB1ENSETR = RCC_MC_APB1ENSETR_I2C2EN;
	(void)RCC->MP_APB1ENSETR;
	i2c_setup(I2C2);
//	dbg();
	// try to set RESX as output
	int res = i2c_wr(I2C2,0x21,"\0\xdf",2,1);
	if (!res) res = i2c_wr(I2C2,0x21,"\x01\xdf",2,1);
	xprintf("SX expader detect err = %d\n",res);
	return !res;
}

static void set_disp_i2c_rst(int x)
{
	// write DATA & DIR register (dir 1=in)
	uint8_t buf[4] = { 0,x?0x20:0,0xdf };
	int r = i2c_wr(I2C2,0x21,buf,2,1);
	xprintf("rst %d i2c wr: %d\n",x,r);
}

static void dsi_stop()
{
	soft_dsi_stop();
//	asm("bkpt");
	set_disp_i2c_rst(0);
}

// implemented in revO
static void disable_emmc_power()
{
	gpio_out(PINo_EMMC_ON,0);
	udelay(100);
	gpio_out(PINo_EMMCIO_ON,0);
}

static void disable_power_n()
{
	dsi_stop(); // also reset display early
	disable_emmc_power();
	gpio_out(PINn_18_DISP,0);
	gpio_out(PINn_SX_RST,0);
	udelay(5000);
	gpio_out(PIN_RUN_ON,0);
	gport_t p5V_ON = brd_rev>='R' ? PINr_50V_ON : PINn_50V_ON;
	gpio_out(p5V_ON,0);
}

static void regain_mpu_control()
{
	// re-enable needed clocks (linux might have disabled them)
	RCC->MP_APB1ENSETR = RCC_MC_APB1ENSETR_I2C2EN;
	RCC->MP_AHB4ENSETR = RCC_MC_AHB4ENSETR_GPIOAEN|RCC_MC_AHB4ENSETR_GPIOBEN|
		RCC_MC_AHB4ENSETR_GPIOCEN|RCC_MC_AHB4ENSETR_GPIODEN|
		RCC_MC_AHB4ENSETR_GPIOEEN|RCC_MC_AHB4ENSETR_GPIOFEN|
		RCC_MC_AHB4ENSETR_GPIOGEN|RCC_MC_AHB4ENSETR_GPIOHEN|
		RCC_MC_AHB4ENSETR_GPIOIEN;
	RCC->MP_APB4ENSETR = RCC_MC_APB4ENSETR_DSIEN;

	i2c_setup(I2C2); // reinit - take control from linux
}

int reset_machine();
void gicc_disable();

// override PSCI power-off handling, mainly to supress
// display glitch
void reset_hook()
{
	regain_mpu_control();
	disable_power_n();	// proper off sequencing
}

static void power_off_r() 
{
	// send I2C power off command to L052 
	int res = i2c_wr(I2C2,0x17,"\x05\x00",2,1);
	if (res) res = i2c_wr(I2C2,0x17,"\x05\x00",2,1); // retry
	xprintf("[%d] off request sent (%X)\n",get_ms_precise(),res);
}

#define BT_INSN_STANDBY 1
// override PSCI power-off handling
int power_off() 
{
	if (brd_rev >= 'R') {
		reset_hook();
		power_off_r();
		for (;;) __WFI();
	}
	// set boot insn and reboot
	mp1_set_boot_insn(BT_INSN_STANDBY);
	reset_hook();
	return reset_machine();
}

// we are called from early booted MPU in secure mode
// without console
static void standby()
{
	int wdg = 0, stbdbg = 0;
	struct stm32mp1_mctx *ctx = (struct stm32mp1_mctx *)mctx;
	int mflg = ctx->boot_flags[BFI_MACH1];
	if (mflg>0) {
		wdg = mflg & 1; stbdbg = mflg & 2;
	}
	somp1_disable_osc();
	if (wdg) RCC->MP_APB5ENSETR = RCC_MP_APB5ENSETR_IWDG1APBEN;

	if (stbdbg) // doesn't shut off completely in DBG mode
		DBGMCU->CR = DBGMCU_CR_DBG_STANDBY|DBGMCU_CR_DBG_STOP;
	else
		DBGMCU->CR = 0;
	// start wdt to wake us (for debug)
	if (wdg) {
		IWDG1->KR = 0xcccc;
		IWDG1->KR = 0x5555;
		IWDG1->RLR = 64*2; // in sec
		udelay(100);
		IWDG1->KR = 0xaaaa;
	}
	RCC->MP_GCR = 0;	// MCU in HOLD_BOOT
	RCC->MP_GRSTCSETR = RCC_MP_GRSTCSETR_MCURST;

	// KEY_ON = PI11/WKUP_5
	PWR->WKUPCR = PWR_WKUPCR_WKUPP_5	// negative polarity
		| 0x3f; 			// clear all wkup pins
	PWR->MPUWKUPENR = PWR_MPUWKUPENR_WKUPEN_5;

	// setup STANDBY request
	PWR->MCUCR = PWR->MPUCR = PWR_MPUCR_PDDS;
	RCC->MP_SREQSETR = RCC_MP_SREQSETR_STPREQ_P0|RCC_MP_SREQSETR_STPREQ_P1;

	//xprintf("WFI %X %X %X\n",DBGMCU->CR,PWR->MPUCR,PWR->MCUCR);
	//udelay(100000);
	__DSB();
	__WFI();
	__DSB();
	asm("bkpt");
}

// hook into early boot sequence
int hook_boot_insn(uint8_t insn,int is_secure)
{
	if (insn == BT_INSN_STANDBY) {
		mp1_set_boot_insn(0);
		standby();
		return 1;
	}
	return 0;
}

// override DSI reset handling
int dsi_disp_reset(int rstn)
{
	if (rstn<0) {
		sx_ok = sx_check_presence();
		return sx_ok ? 0:-1;
	}
	if (!sx_ok) return -1;
	set_disp_i2c_rst(!rstn);
	return 0;
}

static void gen_32k_tim8_1()
{
	// 133MHz input
	RCC->MP_APB2ENSETR = RCC_MC_APB2ENSETR_TIM8EN;
	TIM8->CCMR1 = TIM_CCMR1_OC1M_1|TIM_CCMR1_OC1M_2|TIM_CCMR1_OC1M_0;
	TIM8->ARR = 133333333/32768-1;
	TIM8->CCR1 = TIM8->ARR/2;
	TIM8->CCER = TIM_CCER_CC1E;
	TIM8->BDTR = TIM_BDTR_MOE;
	TIM8->CR1 = TIM_CR1_CEN;
	TIM8->EGR = TIM_EGR_UG;
}

// really it is compatible with rev N...S
static void enable_power_n(void *arg)
{
	const gport_t outs[] = { PIN_RUN_ON, PIN_ETH_RST,PINn_18_DISP,
		PINn_SX_RST,PINo_EMMCIO_ON,PINo_EMMC_ON, 0 };

	gport_t p5V_ON = brd_rev>='R' ? PINr_50V_ON : PINn_50V_ON;
	gpio_setup_same(outs,PM_OUT|PM_DFLT(0),0);
	gpio_setup_one(p5V_ON,PM_OUT|PM_DFLT(0),0);
	udelay(5000);	// power off briefly

	gpio_out(PIN_RUN_ON,1);
	gpio_out(p5V_ON,1);
	udelay(5000);
	gpio_out(PINn_18_DISP,1);
	gpio_out(PINn_SX_RST,1);
	udelay(5000);

	// start LPO clock for wifi
	gpio_setup_one(PINn_CK32,PM_OUT|PM_ALT,3);
	gen_32k_tim8_1();
}

static void eth_phy_init(void *arg)
{
	gpio_out(PIN_ETH_RST,0);

	// start clocking PHY before releasing reset
	gpio_setup_one(PM_B(5),PM_OUT|PM_ALT|PM_SPD(1),0);
	RCC->MP_AHB6ENSETR = RCC_MC_AHB6ENSETR_ETHCKEN;
	
	udelay(10000);
	gpio_out(PIN_ETH_RST,1);
	udelay(10000);

	const struct smi_iodef smid = { PM_E(7),PM_G(10) };
	gpio_setup_one(smid.io_mdc,PM_OUT,0);
	int id = eth_smi_rd(&smid,1,2);
	xprintf("PHY ID: %X\n",id);
}

static int run_portbl(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	detect_rev();
	patch_board_rev(fdt);
	enable_power_n(NULL);
	return 0;
}

static int run_port_eth(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	eth_phy_init(NULL);
	return 0;
}

struct simple_image_t {
	uint16_t w,h,pcnt,tmp;
	uint16_t palsz,pixsz;
} static img;

long tinflate(const void *compressed_data, long compressed_size,
              void *output_buffer, long output_size, unsigned long *crc_ret);
extern char _binary_mach_logo_black_bg_zz_start[];
extern char _binary_mach_logo_black_bg_zz_size[];
extern char _binary_mach_dv_logo_black_bg_zz_start[];
extern char _binary_mach_dv_logo_black_bg_zz_size[];

// decode logo into buf of size max_fb, fill img structure, 
// return 0 if ok; img->w is 0 for error
static int decode_logo(struct simple_image_t *img,void *buf,
		int max_fb,uint32_t image_id)
{
	// binary format is 16bit: W,H,pal_size,0 then compressed
	// palette 3B per item, then pixels (8B aligned)
	// palette is guaranted to have black at index 0
	uint8_t *p = _binary_mach_logo_black_bg_zz_start;
	int rlen = (int)_binary_mach_logo_black_bg_zz_size;
	if (image_id) {
		p = _binary_mach_dv_logo_black_bg_zz_start;
		rlen = (int)_binary_mach_dv_logo_black_bg_zz_size;
	}
	memcpy(img,p,8); p += 8;
	img->palsz = (img->pcnt*3 + 7) & 0xff8;
	img->pixsz = img->h * img->w;

	int sz = tinflate(p,rlen-sizeof(img), buf, max_fb, NULL);
	if (sz < img->pixsz + img->palsz) {
		xprintf("can't decode logo image: %d\n",sz);
		img->w = 0; // mark error
		return -1;
	}
	xprintf("[%d] logo decoded, bytes %d\n",get_ms_precise(),sz);
	return 0;
}

extern struct fbuf_t framebuf;
static void show_logo(const uint8_t *px)
{
	if (!img.w) return;

	// copy image to the center
	int i,j;
	const uint8_t *p = px + img.palsz;
	uint8_t *d = framebuf.fba + (320+img.h)/2 + (320-img.w)/2*320;
	memset(framebuf.fba,0,320*320);
	for (i=0;i<img.h;i++) {
		for (j=0;j<img.w;j++,d+=320) *d = *(p++);
		d -= img.w*320+1;
	}
	p = px;
	for (i=0;i<img.pcnt;i++) { // palette
		int j = i<<24; memcpy(&j,p,3); p += 3;
		LTDC_Layer2->CLUTWR = j;
	}
	LTDC->SRCR = LTDC_SRCR_VBR; // activate layer
	xprintf("[%d] logo done\n",get_ms_precise());
}

static int run_logo(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	// which logo to display (if we have more)
	uint32_t iid = 0;
	fetch_fdt_ints(fdt,root,"image-id",1,1,&iid);

	if (!framebuf.fba || !framebuf.bytes) {
		xprintf("logo: ERROR: bad framebuffer\n");
		return -1;
	}
	void *sram2 = (void*)0x10020000;
	decode_logo(&img,sram2,0x10000,iid);
	show_logo(sram2);
	return 0;
}

DECLARE_MOD(eax_portbl) {
	.name = "eax,portable_n", .run = run_portbl
};
DECLARE_MOD(eax_port_eth) {
	.name = "eax,port_eth", .run = run_port_eth
};

DECLARE_MOD(eax_portbl_logo) {
	.name = "eax,portable_logo", .run = run_logo
};
