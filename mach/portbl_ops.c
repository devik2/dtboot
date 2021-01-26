#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
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
	//i2c_wr(I2C2,0x21,"\x01\xdf",2,1);
}

// override PSCI power-off handling
int power_off() 
{
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
	//asm("bkpt");
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
		PINn_SX_RST, 0 };

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

static void dsi_stop()
{
	set_disp_i2c_rst(0);
}

static void disable_power_n()
{
	dsi_stop();
	gpio_out(PINn_18_DISP,0);
	gpio_out(PINn_SX_RST,0);
	udelay(5000);
	gpio_out(PIN_RUN_ON,0);
	gpio_out(PINn_50V_ON,0);
}

static int run_portbl(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	enable_power_n(NULL);
	eth_phy_init(NULL);
}

DECLARE_MOD(eax_portbl) {
	.name = "eax,portable_n", .run = run_portbl
};

