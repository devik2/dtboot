#include "stm32mp157axx_cm4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "coro.h"
#include "stm32mp1_ltdc.h"
#include "xprintf.h"
#include "fdt.h"
#include "gpio.h"

static struct disp_timing dsi_timing; 
struct fbuf_t {
	uint16_t *fba;
	uint32_t bytes;
} static fbi;

#define printf xprintf

#define DSI_DCS_SHORT_PKT_WRITE_P0 ((uint32_t)0x00000005) /*!< DCS short write, no parameters      */
#define DSI_DCS_SHORT_PKT_WRITE_P1 ((uint32_t)0x00000015) /*!< DCS short write, one parameter      */
#define DSI_GEN_SHORT_PKT_WRITE_P0 ((uint32_t)0x00000003) /*!< Generic short write, no parameters  */
#define DSI_GEN_SHORT_PKT_WRITE_P1 ((uint32_t)0x00000013) /*!< Generic short write, one parameter  */
#define DSI_GEN_SHORT_PKT_WRITE_P2 ((uint32_t)0x00000023) /*!< Generic short write, two parameters */

#define DSI_DCS_LONG_PKT_WRITE ((uint32_t)0x00000039) /*!< DCS long write     */
#define DSI_GEN_LONG_PKT_WRITE ((uint32_t)0x00000029) /*!< Generic long write */

static void DSI_ConfigPacketHeader(DSI_TypeDef *DSIx,
                                   uint32_t ChannelID,
                                   uint32_t DataType,
                                   uint32_t Data0,
                                   uint32_t Data1)
{
  /* Update the DSI packet header with new information */
  DSIx->GHCR = (DataType | (ChannelID<<6) | (Data0<<8) | (Data1<<16));
}

static int was_tmo;
int dsi_wait_fifo(DSI_TypeDef *DSIx)
{
	int tmo = 0;
	while((DSIx->GPSR & DSI_GPSR_CMDFE) == 0) {
		if (++tmo > 10000) {
			xprintf("dsi timeout %X\n",DSIx->GPSR);
			was_tmo++;
			return 1;
		}
		coro_yield();
	}
	udelay(100);
	return 0;
}

int DSI_ShortWrite(DSI_TypeDef *DSIx,
                                 uint32_t ChannelID,
                                 uint32_t Mode,
                                 uint32_t Param1,
                                 uint32_t Param2)
{
  /* Wait for Command FIFO Empty */
  if (dsi_wait_fifo(DSIx)) return -1;
  
  /* Configure the packet to send a short DCS command with 0 or 1 parameter */
  DSI_ConfigPacketHeader(DSIx,
                         ChannelID,
                         Mode,
                         Param1,
                         Param2);
  return 0;
}

/**
  * @brief  DCS or Generic long write command
  * @param  DSIx: To select the DSIx peripheral, where x can be the different DSI instances
  * @param  ChannelID: Virtual channel ID.
  * @param  Mode: DSI long packet data type.
  *               This parameter can be any value of @ref DSI_LONG_WRITE_PKT_Data_Type.
  * @param  NbParams: Number of parameters.
  * @param  Param1: DSC command or first generic parameter.
  *                 This parameter can be any value of @ref DSI_DCS_Command or a 
  *                 generic command code
  * @param  ParametersTable: Pointer to parameter values table.
  * @retval None
  */
int DSI_LongWrite(DSI_TypeDef *DSIx,
                                uint32_t ChannelID,
                                uint32_t Mode,
                                uint32_t NbParams,
                                uint32_t Param1,
                                uint8_t* ParametersTable)
{
  uint32_t uicounter = 0;
  
  if (dsi_wait_fifo(DSIx)) return -1;
  
  /* Set the DCS code hexadecimal on payload byte 1, and the other parameters on the write FIFO command*/
  while(uicounter < NbParams)
  {
    if(uicounter == 0x00)
    {
      DSIx->GPDR=(Param1 | \
                            ((*(ParametersTable+uicounter))<<8) | \
                            ((*(ParametersTable+uicounter+1))<<16) | \
                            ((*(ParametersTable+uicounter+2))<<24));
      uicounter += 3;
    }
    else
    {
      DSIx->GPDR=((*(ParametersTable+uicounter)) | \
                            ((*(ParametersTable+uicounter+1))<<8) | \
                            ((*(ParametersTable+uicounter+2))<<16) | \
                            ((*(ParametersTable+uicounter+3))<<24));
      uicounter+=4;
    }
  }
  
  /* Configure the packet to send a long DCS command */
  DSI_ConfigPacketHeader(DSIx,
                         ChannelID,
                         Mode,
                         ((NbParams+1)&0x00FF),
                         (((NbParams+1)&0xFF00)>>8));
  return 0;
}

int _dm;
void test_dsi()
{
	GPIOD->BSRR = 1<<3;
	DSI_ShortWrite(DSI, 0, DSI_DCS_SHORT_PKT_WRITE_P0, _dm?0x20:0x21, 0);
	GPIOD->BSRR = 0x10000<<3;
	_dm = !_dm;
	asm("bkpt");
}

void stop_dsi()
{
  DSI_ShortWrite(DSI, 0, DSI_DCS_SHORT_PKT_WRITE_P0, 0x28, 0);
  udelay(20000);
  DSI_ShortWrite(DSI, 0, DSI_DCS_SHORT_PKT_WRITE_P0, 0x10, 0);
  udelay(120000);
  RCC->APB4RSTSETR = RCC_APB4RSTSETR_LTDCRST|RCC_APB4RSTSETR_DSIRST;
}

static void check_err(const char *s)
{
	uint32_t a = DSI->ISR[0], b = DSI->ISR[1];
	if (!a && !b) return;
	xprintf("dsi err (%s): %X %X, tmo %d\n",s,a,b,was_tmo);
}

void DispSetup(DSI_TypeDef* DSIx)
{
  uint8_t regData1[]  = {0x55,0xaa,0x52,8,0};
  uint8_t regData2[]  = {0x1,0x90,0x14,0x14,0};
  uint8_t regData2a[] = {0x1,0x90,0x14,0x14,1};
  uint8_t regData3[]  = {7,7,7};
  uint8_t regData4[]  = {0x40};
  uint8_t regData5[]  = {0x55,0xaa,0x52,8,2};
  uint8_t regData6[]  = {8,0x50};
  uint8_t regData7[]  = {0xf2,0x95,4};
  uint8_t regData8[]  = {0x55,0xaa,0x52,8,1};

  uint8_t regData9[]  = {0x22,0x30,0x70};
  uint8_t regData10[]  = {0xf0,0xd4,0x95,0xef,0x4f,0,4};
  uint8_t regData11[]  = {0x17,0x17,0x17,0x17,0x17,0xb};

  uint8_t tmp[3];
#if 0
  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P0, 0x1, 0); // reset
  check_err("reset");
  udelay(100000);
#endif
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 5, 0xF0, regData1);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 5, 0xBD, regData2);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 5, 0xBE, regData2a);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 5, 0xBF, regData2);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 3, 0xBB, regData3);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 1, 0xC7, regData4);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 5, 0xF0, regData5);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 2, 0xFE, regData6);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 3, 0xC3, regData7);
  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P1, 0xCA, 4);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 5, 0xF0, regData8);
  check_err("bk1");
  if (was_tmo) return;
#define TRIPL(R,V) memset(tmp,V,3); DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE,3,R,tmp)
  TRIPL(0xb0,3);
  TRIPL(0xb1,5);
  TRIPL(0xb2,1);
  TRIPL(0xb4,7);
  TRIPL(0xb5,5);
  TRIPL(0xb6,0x53);
  TRIPL(0xb7,0x33);
  TRIPL(0xb8,0x23);
  TRIPL(0xb9,3);
  TRIPL(0xba,0x13);
  check_err("bk2");
  if (was_tmo) return;
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 3, 0xBE, regData9);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 7, 0xCF, regData10);
  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P1, 0x35, 1);
#if 1
  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P1, 0x36, 0x60); // rotation
#else
  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P1, 0x36, 0x0); // rotation
#endif
  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P1, 0xC0, 0x20);
  DSI_LongWrite(DSIx, 0, DSI_DCS_LONG_PKT_WRITE, 6, 0xC2, regData11);

  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P0, 0x11, 0);
  check_err("bk3");
  udelay(10000);
//  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P0, 0x29, 0);
//  DSI_ShortWrite(DSIx, 0, DSI_DCS_SHORT_PKT_WRITE_P0, 0x21, 0); // invert
  check_err("bk4");
}

void set_brightness(int v)
{
  uint8_t regData1[]  = {0x55,0xaa,0x52,8,1};
  uint8_t regData2[]  = {v&0xff,0xd4,0x95,0xef,0x4f,0,4};
  DSI_LongWrite(DSI, 0, DSI_DCS_LONG_PKT_WRITE, 5, 0xF0, regData1);
  DSI_LongWrite(DSI, 0, DSI_DCS_LONG_PKT_WRITE, 7, 0xCF, regData2);
}

void dump_dsi()
{
	struct pairs { int off,cnt; } *p,tab[] = {
		{0,7},{11,25},{37,8},{47,4},{54,2},
		{64,1},{67,2},{70,1},{78,11},
		{256,5},{258,5},{268,1},{0,0} };
	int i;
	volatile unsigned *b = (unsigned*)DSI_BASE;
	for (p = tab;p->cnt;p++) {
		for (i=0;i<p->cnt;i++)
			printf("* %04X = %08X\r\n",4*(i+p->off),b[i+p->off]);
	}
}

struct disp_timing dtim_tab[] = {
	// hf 17180 Hz, vf 48 Hz, old portable (H2)
	{ { 4,20,320,20 },{ 4,16,320,16 }, 6250, 180 },
	// hf 14334 Hz, vf 40 Hz, high quality display
	{ { 4,30,320,80 },{ 4,16,320,16 }, 6250, 120 },
	// hf 21795 Hz, vf 60 Hz, for cheap displays
	{ { 4,30,320,30 }, { 4,19 /* ln offset */,320,70 }, 9571, 180 },
	// hf 19633 Hz, vf 47 Hz, for cheap
	{ { 4,12,320,50 },{ 4,18,320,70 }, 7500, 130 },
};


uint16_t dtim_sum(const struct disp_stim *st,unsigned n)
{
	uint16_t i,s;
	const uint16_t *p = &st->sync;
	for (i=0,s=0;i<n;i++) s += p[i];
	return s;
}

void dtim_set_ltdc(const struct disp_timing *tm)
{
	int i;
	volatile uint32_t *reg = &LTDC->SSCR;
	for (i=0;i<4;i++) 
		reg[i] = ((dtim_sum(&tm->ht,i+1)-1)<<16)|(dtim_sum(&tm->vt,i+1)-1);
}

void dtim_set_dsi(const struct disp_timing *tm)
{
	DSI->VPCR = tm->ht.act; // pix in videpkt (packet size)
	DSI->VHSACR = tm->ht.sync*2;
	DSI->VHBPCR = tm->ht.bp*2;
	DSI->VLCR = dtim_sum(&tm->ht,4)*2;
	DSI->VVSACR = tm->vt.sync;
	DSI->VVFPCR = tm->vt.fp;
	DSI->VVBPCR = tm->vt.bp;
	DSI->VVACR = tm->vt.act;
}

void dtim_dump(const struct disp_timing *tm)
{
	int in_khz = 24000;
	int vco1 = in_khz*((RCC->PLL4CFGR1&0xff)+1)/((RCC->PLL4CFGR1>>16)+1);
	int f_ltdc = vco1 / (((RCC->PLL4CFGR2>>8)&0xff)+1);
	uint32_t dsicr = DSI->WRPCR;
	int f_dsi = in_khz*((dsicr>>2)&0x7f)/((dsicr>>11)&7);
	f_dsi >>= (dsicr>>16)&3;
	int tot_hpix = dtim_sum(&tm->ht,4);
	int hfreq = 1000*f_ltdc/tot_hpix;
	int tot_vpix = dtim_sum(&tm->vt,4);
	int vfreq = hfreq/tot_vpix;
	xprintf("LTDC clk %d kHz, DSI %d kHz, hf %d Hz, vf %d Hz\n",
			f_ltdc,f_dsi,hfreq,vfreq);
}

void ltdc_layer_setup(int idx,const struct disp_timing *tm,
		uint32_t pfcr,uint32_t fba,
		unsigned xoff,unsigned yoff,unsigned xsz,unsigned ysz)
{
	LTDC_Layer_TypeDef *L = idx==1 ? LTDC_Layer1 : LTDC_Layer2;
	// RGB 332 map
	int i,x,y,bpp = pfcr==5?1:2;
	// if we start DSI with uninited CLUTWR, there is mess
	// on display...
	if (pfcr==5) for (i=0;i<256;i++) 
		L->CLUTWR = (i<<24)|((i&0xe0)<<16)|((i&0x1c)<<11)|((i&3)<<6);

	uint16_t vs = dtim_sum(&tm->vt,2), hs = dtim_sum(&tm->ht,2);
	L->WVPCR = ((vs+ysz+yoff-1)<<16)|(vs+yoff);
	L->WHPCR = ((hs+xsz+xoff-1)<<16)|(hs+xoff);
	L->CFBLNR = ysz;
	// at one point we changed 320->318 because there was problem
	// with display at 320px, but we want to keep mem formated as for 320
	uint16_t stride = xsz==318 ? 320:xsz;
	L->CFBLR = ((stride*bpp)<<16)|(xsz*bpp+7);
	L->PFCR = pfcr; // 2=RGB565,5=8bit
	if (fba) {
		L->CFBAR = fba;
		L->CR = LTDC_LxCR_LEN|(pfcr==5?LTDC_LxCR_CLUTEN:0);
	}
}

static void dsi_address(int cmd,uint16_t from,uint16_t to)
{
	uint8_t regData2[4]  = {from>>8,from&255,to>>8,to&255};
	DSI_LongWrite(DSI, 0, DSI_DCS_LONG_PKT_WRITE, 4, cmd, regData2);
	udelay(100);
}

static void dsi_pwrite(int cmd,int cnt,uint32_t px)
{
	uint8_t *mem = alloca(3*cnt);
	int i;
	for (i=0;i<cnt;i++) memcpy(mem+3*i,&px,3);
	DSI_LongWrite(DSI, 0, DSI_DCS_LONG_PKT_WRITE, 3*cnt, cmd, mem);
	udelay(100);
}

static void dsi_cmd_test()
{
	int i;
	for (i=0;i<20;i++) {
		dsi_address(0x2a,0,320);
		dsi_address(0x2b,i*8+10,320);
		dsi_pwrite(0x2c,100,0xff00);
		udelay(10000);
		dsi_pwrite(0x3c,100,0xff);
		udelay(10000);
	}
}

// returns -1 for err, offset if ok
int dsi_compute_pll(uint32_t in_khz,uint32_t out_khz,uint8_t *ion)
{
	int i,o,best=100000;
	for (i=1;i<=7;i++) {
		int infin = in_khz/i;
		if (infin<8000 || infin>50000) continue;
		for (o=1;o<=8;o<<=1) {
			int vco = out_khz*o;
			if (vco<500000 || vco>1000000) continue;
			int n = vco/infin;
			if (n<10 || n>125) continue;
			int f = infin*n/o;
			int diff = abs(f-out_khz);
			if (best>diff) {
				ion[0] = i; ion[1] = o; ion[2] = n; best = diff; 
			}
		}
	}
	return best>=100000 ? -1:best;
}

static void fix_ltdc_pll(int khz)
{
	int in_khz = 24000;
	int vco = in_khz*((RCC->PLL4CFGR1&0xff)+1)/((RCC->PLL4CFGR1>>16)+1);
	int df = vco/khz-1;
	RCC->PLL4CFGR2 = (RCC->PLL4CFGR2 & 0xff00ff)|(df<<8);
}

int dsi_start_pll(int khz)
{
	RCC->MP_APB4ENSETR = RCC_MC_APB4ENSETR_DSIEN|RCC_MC_APB4ENSETR_LTDCEN;
	(void*)RCC->MP_APB4ENSETR;
	RCC->APB4RSTSETR = RCC_APB4RSTSETR_LTDCRST|RCC_APB4RSTSETR_DSIRST;
	udelay(1000);
	RCC->APB4RSTCLRR = RCC_APB4RSTCLRR_DSIRST|RCC_APB4RSTCLRR_LTDCRST;
	udelay(1000);
	DSI->WRPCR = DSI_WRPCR_REGEN|DSI_WRPCR_BGREN;
	while (!(DSI->WISR & DSI_WISR_RRS)) coro_yield();

	uint8_t ion[3];
	const uint8_t o_map[8] = { 0,1,1,2,2,2,2,3 };
	int diff = -1;
	if (khz)
		diff = dsi_compute_pll(24000,khz,ion);
	if (diff<0) {
		xprintf("error computing WPLL, f=%dkHz\n",khz); 
		return -1;
	}
	xprintf("DSI reg ready, WISR %x, pllp %d %d %d\r\n",
			DSI->WISR,ion[0],ion[1],ion[2]);
	DSI->WRPCR |= (ion[2]<<2)| // NDIV
		(ion[0]<<11)| // IDF
		(o_map[ion[1]-1]<<16); // ODF (1,2,4,8)
	DSI->WRPCR |= DSI_WRPCR_PLLEN;
	while (!(DSI->WISR & DSI_WISR_PLLLS)) coro_yield();
	xprintf("[%d] DSI pll locked, WISR %x\r\n",get_ms_precise(),DSI->WISR);
	return 0;
}

// basic DSI 1 lane setup for cmd sending
void setup_dsi_cmd(const struct disp_timing *tm)
{
	DSI->PCTLR = DSI_PCTLR_CKE|DSI_PCTLR_DEN; // DPHY enable
	DSI->PCONFR = 0x3000; // 1 lane, wait 0x30 before hispeed
	DSI->CCR = 0x003; // div pro escape a timeout clock

	DSI->WPCR[0] = 9*4;//|DSI_WPCR0_CDOFFDL; // time interval in 0.25ns

	DSI->PCR = DSI_PCR_BTAE;

	DSI->IER[0] = 0;
	DSI->IER[1] = 0;

	DSI->CLTCR = 0x00a000a;
	DSI->DLTCR = 0x00a0010;
	DSI->LPMCR = 0x100002; // low power packet lens 
	DSI->WCFGR = 1; DSI->MCR = 1; // cmd mode
	DSI->VMCR = DSI_VMCR_VMT1
		|DSI_VMCR_LPCE
		|DSI_VMCR_LPHFPE|DSI_VMCR_LPHBPE
		|DSI_VMCR_LPVAE|DSI_VMCR_LPVFPE
		|DSI_VMCR_LPVBPE|DSI_VMCR_LPVSAE; // mode & LP activity
	DSI->CMCR = 0x10f7f00	// all cmds in low power
		|DSI_CMCR_ARE	// request ACKs
		; 

	DSI->VNPCR = 0xfff; // bytes in NULL pkt
	dtim_set_dsi(tm);

	DSI->LPCR = 4;
	DSI->CR = DSI_CR_EN;
	printf("dsien %X %X\r\n",DSI->ISR[0],DSI->ISR[1]);

	DSI->WCR |= DSI_WCR_DSIEN;
}

void switch_dsi_to_videomode()
{
	printf("switch to video mode %X %X\r\n",DSI->ISR[0],DSI->ISR[1]);
	udelay(10000);
	DSI->WCR = 0; DSI->CR = 0; // stop DSI
	DSI->CLCR = DSI_CLCR_DPCC; // clock lane in HS
	DSI->WCFGR = 0; DSI->MCR = 0; // switch to video mode

	// start in video mode
	DSI->CR = DSI_CR_EN;
	DSI->WCR = DSI_WCR_DSIEN;
}

void presetup_ltdc(const struct disp_timing *tm)
{
	RCC->MP_APB4ENSETR = RCC_MC_APB4ENSETR_LTDCEN;
	RCC->APB4RSTCLRR = RCC_APB4RSTCLRR_LTDCRST;
	dtim_set_ltdc(tm);
	LTDC->BCCR = 255;
	fix_ltdc_pll(tm->pix_khz);
}

void ltdc_start()
{
	LTDC->GCR |= LTDC_GCR_LTDCEN;

	//LTDC->IER = LTDC_IER_RRIE|LTDC_IER_TERRIE|LTDC_IER_FUIE|LTDC_IER_LIE;
	LTDC->LIPCR = 100;
	LTDC->SRCR = LTDC_SRCR_IMR;
	LTDC->BCCR = 0;
	LTDC->BCCR = 0xff00ff;
}

void init_dsi(const struct disp_timing *tm,uint32_t fba)
{
	presetup_ltdc(tm);
	setup_dsi_cmd(tm);

	DispSetup(DSI); // conf. display

	switch_dsi_to_videomode();
	ltdc_start();

	//ltdc_layer_setup(LTDC_Layer1,tm,2,0  ,0,0,320,320);
	//ltdc_layer_setup(LTDC_Layer2,tm,5,fba,0,0,320,320);
	LTDC->SRCR = LTDC_SRCR_VBR;
	dtim_dump(tm);
	udelay(150000);
	// turn display on
	DSI_ShortWrite(DSI, 0, DSI_DCS_SHORT_PKT_WRITE_P0, 0x29, 0);
}

// called with -1 to init I/O, returns <0 for error
__attribute__((weak)) int dsi_disp_reset(int rstn)
{
#define LBR_DRST PM_C(9)
	if (rstn<0) {
		gpio_setup_one(LBR_DRST,PM_OUT|PM_DFLT(1),0);
		return 0;
	}
	gpio_out(LBR_DRST,!rstn);
	return 0;
}

// TODO: currently this is setup for 320x320 1.8" wristwatch DSI display
// it needs to be made more generic
static int run_dsi(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	uint16_t *dti = (uint16_t*)&dsi_timing;
	int i;
#define TIM_SZ 10
	uint32_t ph[TIM_SZ];
	if (fetch_fdt_ints(fdt,root,"dsi-timing",TIM_SZ,TIM_SZ,ph)!=TIM_SZ)
		return -1;
	for (i=0;i<TIM_SZ;i++) dti[i] = ph[i];

	if (dsi_disp_reset(-1)<0) {
		printf("can't init display reset line\r\n");
		return -1;
	}

	dsi_start_pll(dsi_timing.dsi_mhz*1000);

	fbi.fba = NULL; 
	fetch_fdt_ints(fdt,root,"fb-info",2,2,(uint32_t*)&fbi);

	if (fbi.fba && fbi.bytes) 
		memset(fbi.fba,0x10,fbi.bytes); // clear framebuffer
	dsi_disp_reset(1);
	udelay(10000);
	dsi_disp_reset(0);
	udelay(10000);
	init_dsi(&dsi_timing,(uint32_t)fbi.fba);
	ltdc_layer_setup(1,&dsi_timing,5,(uint32_t)fbi.fba,1,1,318,318);
	//LTDC->SRCR = LTDC_SRCR_VBR;

	//asm("bkpt");
	return 0;
}

DECLARE_MOD(mp1_dsi) {
	.name = "mp1,dsi", .run = run_dsi
};

