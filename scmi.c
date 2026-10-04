#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "system.h"
#include "scmi.h"

extern void dbg_print(const char *s, unsigned x, const char *e);

static const char hex_tab[16] = "0123456789ABCDEF";

#define SCMI_SHM_SZ 128
struct scmi_shared_mem {
        uint32_t reserved;
        uint32_t channel_status;
#define SCMI_SHMEM_CHAN_STAT_CHANNEL_ERROR      BIT(1)
#define SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE       BIT(0)
        uint32_t reserved1[2];
        uint32_t flags;
#define SCMI_SHMEM_FLAG_INTR_ENABLED    BIT(0)
        uint32_t length;
        uint32_t msg_header;
        uint8_t msg_payload[];
};
#define SCMIM ((struct scmi_shared_mem*)0x20060000)
#define SCMI_R_OK 0
#define SCMI_R_NOT_SUPP -1
#define SCMI_R_NOT_FOUND -4
#define SCMI_PROT_VERS 0
#define SCMI_PROT_ATTR 1
#define SCMI_PROT_BASE_VEND 3
#define SCMI_PROT_BASE_IVER 5
#define SCMI_PROT_BASE_PLIST 6

#define SCMI_P_BASE 0x10
#define SCMI_P_CLK 0x14
#define SCMI_P_RST 0x16
#define SCMI_PROTO_CNT 2	// excluding base proto


static void scmi_cache_clean()
{
	// assume 128B long cacheline aligned SCMIM
	asm volatile("dc  cvac,%0\nadd x3,%0,#64\ndc  cvac,x3\ndsb ish":
			:"r"(SCMIM):"memory","x3");
}

static void scmi_cache_inv()
{
	// assume 128B long cacheline aligned SCMIM
	asm volatile("dc civac,%0\nadd x3,%0,#64\ndc civac,x3\ndsb ish":
			:"r"(SCMIM):"memory","x3");
}

void scmi_init()
{
	static uint8_t scmi_prepared;
	if (scmi_prepared) return;

	scmi_prepared = 1;
	memset(SCMIM,0,SCMI_SHM_SZ);
	SCMIM->channel_status = 1; // mark as free
	scmi_cache_clean();
	dbg_print("SCMI INIT ",SCMIM->channel_status,"\r\n");
}

// returns no of valid args in rply
static uint32_t scmi_base(uint32_t cmd,uint32_t *arg)
{
	const char nm[] __attribute__((aligned(8))) = "DTboot2";
	switch (cmd) {
		case SCMI_PROT_VERS:
			arg[0] = SCMI_R_OK; arg[1] = 0x10000; return 2;
		case SCMI_PROT_ATTR:
			arg[0] = SCMI_R_OK; arg[1] = SCMI_PROTO_CNT;
			return 2;
		case SCMI_PROT_BASE_VEND:
			arg[0] = SCMI_R_OK; arg[1] = ((uint32_t*)nm)[0];
			arg[2] = ((uint32_t*)nm)[1];
			return 3;
		case SCMI_PROT_BASE_IVER:
			arg[0] = SCMI_R_OK; arg[1] = 0xab7; return 2;
		case SCMI_PROT_BASE_PLIST:
			arg[1] = SCMI_PROTO_CNT;
			arg[2] = SCMI_P_CLK|(SCMI_P_RST<<8);
			arg[0] = SCMI_R_OK; return 3;
		default: arg[0] = SCMI_R_NOT_SUPP; return 1;
	}
}

static const uint32_t base_hz[16] = {
	375000000,0,0,0,0, // PLL4..8
	64000000,40000000,0, // hsi,hse,msi
	64000000,40000000,0,
	0,0, // spif,i2s
	32000,32768,0 // LSI, LSE
};

static uint32_t flx_clk_rate(uint32_t ch)
{
	ch &= 63;
	int fin = RCC->FINDIVxCFGR[ch];
	int xbar = RCC->XBARxCFGR[ch];
	if (!(fin & RCC_FINDIVxCFGR_FINDIVxEN) || !(xbar & RCC_XBARxCFGR_XBARxEN))
		return 0; // disabled
	fin &= RCC_FINDIVxCFGR_FINDIVx_Msk;
	fin++;
	fin *= (RCC->PREDIVxCFGR[ch] & RCC_PREDIVxCFGR_PREDIVx_Msk)+1;
	return base_hz[xbar & 15]/fin;
}

static uint32_t scmi_clk_rate(uint32_t id)
{
	if (id <= 63) return flx_clk_rate(id);
	if (id >= CK_SCMI_TIMG1 && id<= CK_SCMI_TIMG2)
		id -= CK_SCMI_TIMG1-CK_SCMI_ICN_APB1; // TODO: respect correct LUT
	if (id >= CK_SCMI_ICN_APB1 && id <= CK_SCMI_ICN_APBDBG) {
		volatile uint32_t *r = &RCC->APB1DIVR;
		uint32_t d = r[id-CK_SCMI_ICN_APB1] & 7;
		uint32_t flx0 = flx_clk_rate(0);
		if (RCC->LSMCUDIVR & 1) d++;
		return flx0 >> d;
	}
	switch (id) {
		case CK_SCMI_ICN_LS_MCU:
			return flx_clk_rate(0)/((RCC->LSMCUDIVR&1)+1);
		case CK_SCMI_HSE: return base_hz[6];
		case CK_SCMI_LSE: return base_hz[14];
		case CK_SCMI_HSI: return base_hz[5];
		case CK_SCMI_LSI: return base_hz[13];
		case CK_SCMI_MSI: return base_hz[7];
		case CK_SCMI_HSE_DIV2: return base_hz[6]/2;

		case CK_SCMI_IPCC1: return base_hz[5]; // TODO: fake value
	}
	return 0;
}

static const char clk_names[][8] __attribute__((aligned(8))) =
	{ "ls_mcu","sc_hse","sc_lse","sc_hsi","sc_lsi",
	"sc_msi","sc_hse2","sc_cpu1","sc_scpu","sc_pll2","sc_pll3",
	"sc_rtc","sc_rtcc","sc_apb1","sc_apb2","sc_apb3","sc_apb4","sc_apbd",
	"sc_tig1","sc_tig2"
};

/* RCC CFGR offsets for CK_SCMI_BKPSRAM(84)..CK_SCMI_STM500(122), 0=no gate */
static const uint16_t clk_cfgr[] = {
	0x504, 0x584,    0, 0x528, 0x51C, /* BKPSRAM,BSEC,[86],ETR,FMC */
	0x52C, 0x530, 0x534, 0x538, 0x53C, 0x540, 0x544, 0x548, /* GPIOA-H */
	0x54C, 0x550, 0x554, 0x558,                              /* GPIOI-Z */
	0x55C, 0x560, 0x564,                                     /* HPDMA1-3 */
	0x56C, 0x570, 0x574, 0x568,                              /* HSEM,IPCC1,IPCC2,LPDMA */
	0x500, 0x4F8, 0x4FC, 0x508, 0x50C, 0x510,               /* RETRAM,SRAM1-2,LPSRAM1-3 */
	0x4F4, 0x4F0, 0x514, 0x518,                              /* VDERAM,SYSRAM,OSPI1-2 */
	   0,     0,     0,     0,     0,                        /* TPIU,SYSDBG,SYSATB,TSDBG,STM500 */
};

static int scmi_clk_enable(uint32_t id, int on)
{
	if (id < CK_SCMI_BKPSRAM || id > CK_SCMI_STM500) return 0;
	uint16_t off = clk_cfgr[id - CK_SCMI_BKPSRAM];
	if (!off) return -1;
	volatile uint32_t *r = (volatile uint32_t*)((uint8_t*)RCC + off);
	if (on) *r |= 2; else *r &= ~2;
	return 0;
}

#define SCMI_PROT_CLK_ATTR 3
#define SCMI_PROT_CLK_RGET 6
#define SCMI_PROT_CLK_CFSET 7

static uint32_t scmi_clk(uint32_t cmd,uint32_t *arg)
{
	char nm[8] __attribute__((aligned(8))) = "flxg_..";
	nm[5] = hex_tab[(arg[0] >> 4) & 15];
	nm[6] = hex_tab[(arg[0] >> 0) & 15];
	if (arg[0]>CK_SCMI_FLEXGEN_63 && arg[0]<=CK_SCMI_TIMG2) {
		uint64_t *p = (uint64_t*)clk_names[arg[0]-64];
		*(uint64_t*)nm = *p;
	}
	if (cmd >= SCMI_PROT_CLK_ATTR && arg[0] >= SCMI_CLK_CNT) {
		arg[0] = SCMI_R_NOT_FOUND;
		return 1;
	}
	switch (cmd) {
		case SCMI_PROT_VERS:
			arg[0] = SCMI_R_OK; arg[1] = 0x10000; return 2;
		case SCMI_PROT_ATTR:
			arg[0] = SCMI_R_OK; arg[1] = SCMI_CLK_CNT;
			return 2;
		case SCMI_PROT_CLK_ATTR:
			arg[1] = scmi_clk_rate(arg[0])>0;
			arg[2] = *(uint32_t*)nm;
			arg[3] = ((uint32_t*)nm)[1];
			arg[0] = SCMI_R_OK; return 4;
		case SCMI_PROT_CLK_RGET:
			arg[1] = scmi_clk_rate(arg[0]); arg[2] = 0;
			arg[0] = SCMI_R_OK; return 3;
		case SCMI_PROT_CLK_CFSET:
			arg[0] = scmi_clk_enable(arg[0], arg[1] & 1)
				? SCMI_R_NOT_FOUND : SCMI_R_OK;
			return 1;
		default: arg[0] = SCMI_R_NOT_SUPP; return 1;
	}
}

#define SCMI_PROT_RST_ATTR 3
#define SCMI_PROT_RST_DO 4
#define SCMI_RST_CNT 9

static void scmi_rst_do(uint32_t domain, uint32_t flags)
{
	int assert = flags & 2; /* EXPLICIT_RESET_ASSERT = BIT(1) */
	switch (domain) {
	case RST_SCMI_C2_R:
		if (assert) RCC->C2RSTCSETR = RCC_C2RSTCSETR_C2RST;
		break;
	case RST_SCMI_C2_HOLDBOOT_R:
		/* RCC_CPUBOOTCR bit0: 1=run, 0=hold */
		if (assert)
			RCC->CPUBOOTCR &= ~RCC_CPUBOOTCR_BOOT_CPU2;
		else
			RCC->CPUBOOTCR |=  RCC_CPUBOOTCR_BOOT_CPU2;
		break;
	}
}

static uint32_t scmi_rst(uint32_t cmd,uint32_t *arg)
{
	char nm[8] __attribute__((aligned(8))) = "scrs_..";
	nm[5] = hex_tab[(arg[0] >> 4) & 15];
	nm[6] = hex_tab[(arg[0] >> 0) & 15];
	//dbg_print("SCMI RST ",(cmd<<24)|arg[0]|(arg[1]<<16),"\n");
	if (cmd >= SCMI_PROT_RST_ATTR && arg[0] >= SCMI_RST_CNT) {
		arg[0] = SCMI_R_NOT_FOUND;
		return 1;
	}
	switch (cmd) {
		case SCMI_PROT_VERS:
			arg[0] = SCMI_R_OK; arg[1] = 0x30000; return 2;
		case SCMI_PROT_ATTR:
			arg[0] = SCMI_R_OK; arg[1] = SCMI_RST_CNT;
			return 2;
		case SCMI_PROT_RST_ATTR:
			arg[1] = 0; arg[2] = -1;
			arg[3] = *(uint32_t*)nm;
			arg[4] = ((uint32_t*)nm)[1];
			arg[0] = SCMI_R_OK; return 5;
		case SCMI_PROT_RST_DO:
			scmi_rst_do(arg[0], arg[1]);
			arg[0] = SCMI_R_OK; return 1;
		default: arg[0] = SCMI_R_NOT_SUPP; return 1;
	}
}

__attribute__((noinline))
void invoke_scmi(uint32_t t1,uint32_t t2)
{
	struct scmi_shared_mem *shm = SCMIM;
	scmi_cache_inv();
	if (shm->channel_status) return; // not busy !?
	uint32_t *arg = (uint32_t*)shm->msg_payload;
	uint32_t proto = (shm->msg_header >> 10) & 0xff;
	uint32_t cmd = shm->msg_header & 0xff;
	uint32_t rply = 1;
	switch (proto) {
		case SCMI_P_BASE: rply = scmi_base(cmd,arg); break;
		case SCMI_P_CLK:  rply = scmi_clk(cmd,arg);  break;
		case SCMI_P_RST:  rply = scmi_rst(cmd,arg);  break;
		default: arg[0] = SCMI_R_NOT_SUPP; break;
	}
	shm->length = rply*4 + 4;
	shm->channel_status = 1;
	scmi_cache_clean();
}
