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

#define PINn_18_DISP PM_B(9)
#define PINn_50V_ON PM_H(13)
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
	gpio_out(PINn_50V_ON,0);
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

// override PSCI power-off handling, mainly to supress
// display glitch
void reset_hook()
{
	regain_mpu_control();
	disable_power_n();	// proper off sequencing
}

void gicc_disable();

// override PSCI power-off handling
int power_off() 
{
	const int wdg = 0;	// start wdt to wake us (for debug)

	// make sure interrupt doesn't mess with our sleep
	gicc_disable();
	regain_mpu_control();
	if (wdg) RCC->MP_APB5ENSETR = RCC_MP_APB5ENSETR_IWDG1APBEN;

	if (0) // doesn't shut off completely in DBG mode
		DBGMCU->CR = DBGMCU_CR_DBG_STANDBY|DBGMCU_CR_DBG_STOP;
	else
		DBGMCU->CR = 0;
	if (wdg) {
		IWDG1->KR = 0xcccc;
		IWDG1->KR = 0x5555;
		IWDG1->RLR = 32*2; // 1sec
		udelay(100);
		IWDG1->KR = 0xaaaa;
	}
	disable_power_n();

	RCC->MP_GCR = 0;	// MCU in HOLD_BOOT
	RCC->MP_GRSTCSETR = RCC_MP_GRSTCSETR_MCURST;

	// Note: standby is not working with SMP when second CPU is
	// in WFE (in arch/arm/kernel/smp.c). Actual workaround
	// is to use echo 0 > /sys/devices/system/cpu/cpu1/online and
	// use RCC_MP_GRSTCSETR_MPUP1RST.
	// Also note that RCC_MP_GRSTCSETR_MPUP1RST can't be used
	// alone in this function because real reset is applied only
	// after subsequent WFI. devik

	// KEY_ON = PI11/WKUP_5
	PWR->WKUPCR = PWR_WKUPCR_WKUPP_5	// negative polarity
		| 0x3f; 			// clear all wkup pins
	PWR->MPUWKUPENR = PWR_MPUWKUPENR_WKUPEN_5;

	// setup STANDBY request
	PWR->MCUCR = PWR->MPUCR = PWR_MPUCR_PDDS;
	RCC->MP_SREQSETR = RCC_MP_SREQSETR_STPREQ_P0|RCC_MP_SREQSETR_STPREQ_P1;

	xprintf("WFI %X %X %X\n",DBGMCU->CR,PWR->MPUCR,PWR->MCUCR);
	udelay(100000);
	__WFI();
	xprintf("WFI done %X %X %X\n",DBGMCU->CR,PWR->MPUCR,PWR->MCUCR);
	asm("bkpt");
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

static void enable_power_n(void *arg)
{
	const gport_t outs[] = { PINn_50V_ON,PIN_RUN_ON, PIN_ETH_RST,PINn_18_DISP,
		PINn_SX_RST,PINo_EMMCIO_ON,PINo_EMMC_ON, 0 };

	gpio_setup_same(outs,PM_OUT|PM_DFLT(0),0);
	udelay(5000);	// power off briefly

	gpio_out(PIN_RUN_ON,1);
	gpio_out(PINn_50V_ON,1);
	udelay(5000);
	gpio_out(PINn_18_DISP,1);
	gpio_out(PINn_SX_RST,1);
	udelay(5000);

	// start LPO clock for wifi
	gpio_setup_one(PINn_CK32,PM_OUT|PM_ALT,3);
	gen_32k_tim8_1();
	coro_done();
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

	coro_done();
}

static int run_portbl(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	enable_power_n(NULL);
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

// decode logo into buf of size max_fb, fill img structure, 
// return 0 if ok; img->w is 0 for error
static int decode_logo(struct simple_image_t *img,void *buf,int max_fb)
{
	// binary format is 16bit: W,H,pal_size,0 then compressed
	// palette 3B per item, then pixels (8B aligned)
	// palette is guaranted to have black at index 0
	uint8_t *p = _binary_mach_logo_black_bg_zz_start;
	int rlen = (int)_binary_mach_logo_black_bg_zz_size;

	struct {
		uint16_t w,h,pcnt,tmp;
	} hdr;
	memcpy(img,p,8); p += 8;
	img->palsz = (img->pcnt*3 + 7) & 0xff8;
	img->pixsz = img->h * img->w;

	int sz = tinflate(p,rlen-sizeof(hdr), buf, max_fb, NULL);
	if (sz < img->pixsz + img->palsz) {
		xprintf("can't decode logo image: %d\n",sz);
		img->w = 0; // mark error
		return -1;
	}
	xprintf("[%d] logo decoded, bytes %d\n",get_ms_precise(),sz);
	return 0;
}

static void show_logo(char *fba)
{
	if (!img.w) return;

	// move image to the center
	int i;
	uint8_t *p = fba + img.palsz;
	uint8_t *d = fba + (320-img.w)/2 + (320-img.h)/2*320;
	for (i=0;i<img.h;i++) {
		memcpy(d,p,img.w); p += img.w; d += 320;
	}
	p = fba;
	for (i=0;i<img.pcnt;i++) { // palette
		int j = i<<24; memcpy(&j,p,3); p += 3;
		LTDC_Layer2->CLUTWR = j;
	}
	memset(fba,0,img.palsz+img.pixsz); // clear old image
	LTDC->SRCR = LTDC_SRCR_VBR; // activate layer
	xprintf("[%d] logo done\n",get_ms_precise());
}

extern struct fbuf_t framebuf;
static int run_logo(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	if (!framebuf.fba || !framebuf.bytes) {
		xprintf("logo: ERROR: bad framebuffer\n");
		return -1;
	}
	decode_logo(&img,framebuf.fba,framebuf.bytes);
	show_logo(framebuf.fba);
	return 0;
}

DECLARE_MOD(eax_portbl) {
	.name = "eax,portable_n", .run = run_portbl
};

DECLARE_MOD(eax_portbl_logo) {
	.name = "eax,portable_logo", .run = run_logo
};
