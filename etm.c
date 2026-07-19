#include <string.h>
#include <stdint.h>
#include "stm32mp1.h"
#include "system.h"

extern void dbg_print(const char *s, unsigned x, const char *e);

/* CoreSight component base addresses - system bus */
#define CS_DBGMCU_BASE  0x4A010000
#define FUNNEL_BASE     0x4A020000
#define ETF_BASE        0x4A030000
#define ETR_BASE        0x4A040000
#define TPIIU_BASE      0x4A050000
#define REPLICATOR_BASE 0x4A060000
#define TIMESTAMP_BASE  0x4A070000
#define STM_BASE        0x4A080000
#define STM_PROBE       0x4A800000
#define CTI0_BASE       0x4A090000
#define CTI1_BASE       0x4A0A0000
#define SWO_BASE        0x4A0B0000

/* Cortex-A35 core 0 */
#define CA35_0_DBG_BASE     0x4A210000
#define CA35_0_CTI_BASE     0x4A220000
#define CA35_0_PMU_BASE     0x4A230000
#define CA35_0_ETM_BASE     0x4A240000

/* Cortex-A35 core 1 */
#define CA35_1_DBG_BASE     0x4A310000
#define CA35_1_CTI_BASE     0x4A320000
#define CA35_1_PMU_BASE     0x4A330000
#define CA35_1_ETM_BASE     0x4A340000

/* CoreSight common */
#define CS_LAR          0xFB0
#define CS_UNLOCK       0xC5ACCE55U

/* TMC (ETF) offsets */
#define TMC_CTL         0x020   /* bit0 = TraceCaptEn */
#define TMC_MODE        0x028   /* 0 = circular buffer */
#define TMC_FFCR        0x304   /* formatter/flush control */
#define TMC_STS         0x00C   /* bit2 = TMCReady */

/* Funnel offsets */
#define FUNNEL_CTRL     0x000   /* bits[7:0] = port enables, bits[10:8] = hold time */

/* ETMv4 offsets */
#define ETM_PRGCTLR     0x004   /* bit0 = EN (0 = programming mode) */
#define ETM_STATR       0x00C   /* bit0 = idle */
#define ETM_CONFIGR     0x010
#define ETM_EVENTCTL0R  0x020
#define ETM_EVENTCTL1R  0x024
#define ETM_STALL	0x02c
#define ETM_SYNCPR      0x034   /* sync period (log2 instructions) */
#define ETM_BBCTLR	0x03c
#define ETM_TRACEIDR    0x040   /* trace stream ID */
#define ETM_VICTLR      0x080   /* ViewInst control */
#define ETM_VIIECTLR    0x084   /* ViewInst include/exclude */
#define ETM_VISSCTLR    0x088
#define ETM_OSLAR	0x300
/* Address comparator value registers: low word at 0x400+n*8, high at 0x404+n*8 */
#define ETM_ACVR_L(n)   (0x400 + (n) * 8)
#define ETM_ACVR_H(n)   (0x404 + (n) * 8)
#define ETM_ACATR(n)    (0x480 + (n) * 8)

#define ETM_VICTLR_SS   0x200
#define ETM_VICTLR_RST  0x400
#define ETM_VICTLR_ERR  0x800
#define ETM_VICTLR_ENS(L)  (1<<(20+L)) // dont trace in NS ELx

#define STM_TCSR	0xe80 // glbl en
#define STM_SYNCR	0xe90
#define STM_SPER	0xe00 // port enable
#define STM_SPTER	0xe20 // port enable

static inline volatile uint32_t *csreg(uintptr_t base, uint32_t off)
{
	return (volatile uint32_t *)(base + off);
}

static void cs_unlock(uintptr_t base)
{
	*csreg(base, CS_LAR) = CS_UNLOCK;
	__asm__ volatile("dsb sy" ::: "memory");
}

static void stm_init(uintptr_t base)
{
	cs_unlock(base);
	*csreg(base, STM_SYNCR) = 0; // no syncs
	*csreg(base, STM_SPER) = 0xffffffff;
	*csreg(base, STM_SPTER) = 0xffffffff;
	*csreg(base, STM_TCSR) = 0x20001; // EN with ID 2
	*csreg(STM_PROBE, 0x18) = 77; // wr data
	*csreg(STM_PROBE, 0x18) = 88; // wr data
}

static void etf_init(uintptr_t base)
{
	cs_unlock(base);
	/* disable to allow configuration */
	*csreg(base, TMC_CTL) = 0;
	/* wait until ready */
	while (!(*csreg(base, TMC_STS) & (1u << 2)));
	//*csreg(base, TMC_MODE) = 1; // sw fifo
	*csreg(base, TMC_MODE) = 0; // circbuf
	*csreg(base, TMC_FFCR) = 0x1003; // use fmt,insrt TRGs,stpOnFl
	/* enable capture */
	*csreg(base, TMC_CTL) = 1;
}

static void funnel_enable_port(uintptr_t base, int msk)
{
	cs_unlock(base);
	/* hold time = 4 cycles (bits[10:8]=0x3 means 4), enable port */
	uint32_t v = *csreg(base, FUNNEL_CTRL);
	v = (v & ~0x7FFu) | (0x3u << 8) | msk;
	*csreg(base, FUNNEL_CTRL) = v;
}

void etm_comp(int comp, uint64_t val)
{
	uintptr_t base = CA35_0_ETM_BASE;
	cs_unlock(base);
	*csreg(base, ETM_ACVR_L(comp)) = (uint32_t)val;
	*csreg(base, ETM_ACVR_H(comp)) = (uint32_t)(val >> 32);
	*csreg(base, ETM_ACATR(comp))  = 0;
}

static void etm0_init(uintptr_t base)
{
	cs_unlock(base);

	/* enter programming mode */
	*csreg(base, ETM_PRGCTLR) = 0;
	/* wait for idle */
	while (!(*csreg(base, ETM_STATR) & (1u << 0)));

	/* no cycle count, no timestamps, no context ID */
	*csreg(base, ETM_CONFIGR) = 1; // en BB
	//*csreg(base, ETM_CONFIGR) = 0x18c1;
	/* no events */
	*csreg(base, ETM_EVENTCTL0R) = 0;
	*csreg(base, ETM_EVENTCTL1R) = 0;
	*csreg(base, ETM_STALL) = 0;
	/* sync every 1024 instructions */
	*csreg(base, ETM_SYNCPR) = 16;
	/* trace stream ID = 1 */
	*csreg(base, ETM_TRACEIDR) = 1;
	/* include all instructions, start/stop override on */
	etm_comp(0,0xe002600);
	etm_comp(1,0xe018000);
	*csreg(base, ETM_VIIECTLR) = 1;
	*csreg(base, ETM_BBCTLR) = 0x101;
	*csreg(base, ETM_VICTLR) = ETM_VICTLR_ERR|ETM_VICTLR_RST|ETM_VICTLR_SS
		|ETM_VICTLR_ENS(0)| 1;
	*csreg(base, ETM_VISSCTLR) = 0;

	*csreg(base, ETM_OSLAR) = 0; // unlock

	__asm__ volatile("isb" ::: "memory");
	/* enable ETM */
	*csreg(base, ETM_PRGCTLR) = 1;
}

void etm_init()
{
	/* power up debug for CPU0 */
	CA35SYSCFG->DBGPWR_CR |= CA35SYSCFG_DBGPWR_CR_DBGPWRUP0;
	/* wait for debug power-up ack */
	//while (!(CA35SYSCFG->DBGPWR_SR & CA35SYSCFG_DBGPWR_CR_DBGPWRUP0));

	etf_init(ETF_BASE);
	funnel_enable_port(FUNNEL_BASE, 5);
	stm_init(STM_BASE);
	etm0_init(CA35_0_ETM_BASE);
}
