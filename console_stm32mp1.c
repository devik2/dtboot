#include <string.h>
#include <stdarg.h>
#include "stm32mp1.h"
#include "xprintf.h"
#include "coro.h"
#include "gpio.h"
#include "fdt.h"
#include "ddr_mp2/stm32mp2xx_ll_rcc.h"

static uint8_t *tbuf;

static void trace(uint8_t ev,uint8_t a1,uint8_t a2)
{
	if (!tbuf) {
		tbuf = (void*)0x10000000;
		memset(tbuf,0,0x50000);
	}
	if ((int)tbuf>=0x10050000) return;
	tbuf[0] = ev;
	tbuf[1] = coro_current;
	tbuf[2] = a1;
	tbuf[3] = a2;
	tbuf += 4;
}

static char utxbuf[256]; // don't resize !
static volatile uint8_t utx_wr,utx_rd;
char cons_do_buf;

static void uart_putc_buf(struct _xfunc_out_t *f,unsigned char c)
{
	while ((uint8_t)(utx_wr+1) == utx_rd) coro_yield();
	utxbuf[utx_wr++] = c;
}

#define MAX_UAS 2
static USART_TypeDef *dusart[MAX_UAS]; // debug UARTs (UART1 typ.), optional
static void uart_putc(struct _xfunc_out_t *f,unsigned char c)
{
	int i;
	for (i=0;i<MAX_UAS;i++) {
		if (!dusart[i]) continue; 
		while (!(dusart[i]->ISR & USART_ISR_TXE_TXFNF));
		dusart[i]->TDR = c;
	}
}

static void uart_thread(void *arg)
{
	int i;
	for (;;) {
		while (utx_wr == utx_rd) coro_yield();
		// TODO: send more bytes
		for (i=0;i<MAX_UAS;i++) {
			if (!dusart[i]) continue; 
			if (!(dusart[i]->ISR & USART_ISR_TXE_TXFNF)) break;
		}
		if (i<MAX_UAS) {
			coro_yield();
			continue; // some port not ready
		}
		char c = utxbuf[utx_rd++];
		for (i=0;i<MAX_UAS;i++) if (dusart[i]) dusart[i]->TDR = c;
	}
}

static char cons_task;
static char inited;
static void init_usart_once()
{
	if (inited) return;
	inited = 1;
	if (cons_do_buf) {
		xstream_out.putc = uart_putc_buf;
		cons_task = coro_new(uart_thread,NULL,0,"console");
	} else 
		xstream_out.putc = uart_putc;
}

void console_sync()
{
	if (cons_do_buf) while (utx_wr != utx_rd) coro_yield();
	while (!(dusart[0]->ISR & USART_ISR_TC));
}

// used when coroutines support ends (when starting Linux)
void console_stop_buf()
{
	console_sync();
	if (!cons_task) return;
	coro_kill(cons_task); cons_task = 0;
	cons_do_buf = 0;
	xstream_out.putc = uart_putc;
	xprintf("Console thread stopped\n");
}

// stops all UARTs completely
void console_stop()
{
	console_stop_buf();
	int i;
	for (i=0;i<MAX_UAS;i++) if (dusart[i]) dusart[i]->CR1 = 0;
	inited = 0;
}

void init_usart(int id,USART_TypeDef *usart,int div,int buf)
{
	if (IS_LPUART_INSTANCE(usart)) div *= 256;
	usart->BRR = div;
	usart->CR1 = USART_CR1_UE|USART_CR1_TE|USART_CR1_FIFOEN;
	dusart[id&1] = usart;
	cons_do_buf = buf;
	init_usart_once();
}

struct mp1_uart_txs_t {
	uint8_t pin;
	uint8_t	uart:4;
	uint8_t	conf:4;
};

struct mp1_uart_txs_t static const mp25_uart_txs[32] = {
	{ PM_Z(0),10,6 }, 
	{ PM_Z(7),10,6 }, // in revC
	{ 0,0,0 }
};

// sets given TX line up and returns UART id (0 for error)
// if up=0, pin is de-setup (set as analog)
uint32_t mp1_uart_tx_setup(uint32_t seq,int up)
{
	if (seq>=32) return 0;
	const struct mp1_uart_txs_t *u = mp25_uart_txs + seq;
	if (!u->pin) return 0;
	gpio_setup_one(u->pin,up ? PM_OUT|PM_ALT:PM_AN,u->conf);
	return u->uart;
}

struct mp1_usart_info_t {
	uint32_t mem_off;
	uint8_t fck_chan;
};
struct mp1_usart_info_t static const mp25_usart_info[] = {
	{ USART1_BASE,19 },
	{ USART2_BASE,8 },
	{ USART3_BASE,9 },
	{ UART4_BASE,8 },
	{ UART5_BASE,9 },
	{ USART6_BASE,20 },
	{ UART7_BASE,21 },
	{ UART8_BASE,21 },
	{ UART9_BASE,22 },
	{ LPUART1_BASE,39 }
}; 

// returns USART pointer and sets its clock to clk (0=HSI,1=HSE)
// uid 10 is LPUART1
USART_TypeDef *mp1_get_uart(uint32_t uid,uint32_t clk)
{
	if (!uid || uid>10) return NULL;

	const struct mp1_usart_info_t *u = mp25_usart_info + uid-1;
	if (!u->mem_off) return NULL;
	rcc_flx_chan_set(u->fck_chan,clk ? LL_RCC_XBAR_CLKSRC_HSE:LL_RCC_XBAR_CLKSRC_HSI,1,1);
	rcc_en_clock(&RCC->USART1CFGR+uid-1);
	return (USART_TypeDef*)(uintptr_t)u->mem_off;
}

static int run_console(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	uint32_t baud = 115200;
	fetch_fdt_ints(fdt,root,"baud-rate",1,1,&baud);
	uint32_t hse_fq = 0; // 0 means HSI
	fetch_fdt_ints(fdt,root,"mp1,hse-khz",1,1,&hse_fq);
	uint32_t sync = 0; 
	fetch_fdt_ints(fdt,root,"sync-console",0,0,&sync);
sync=1;//TODO
	struct stm32mp1_mctx *ctx = (struct stm32mp1_mctx *)mctx;
	int uid = ctx->boot_flags[BFI_UARTL];
	if (uid<0) uid = ctx->boot_flags[BFI_UARTH]+16;

	int nid = uid;
	fetch_fdt_ints(fdt,root,"uid",1,1,(uint32_t*)&nid);

	// always remove old and setup new even if they are the same
	// because probably at least clock source changed
	console_stop();
	mp1_uart_tx_setup(uid,0);
	int uart = mp1_uart_tx_setup(nid,1);

	int warn = 0;
	if (!uart) { // bad uart ? use old one
		uart = mp1_uart_tx_setup(uid,1);
		warn = 1;
	}
	USART_TypeDef *u = mp1_get_uart(uart,!!hse_fq);
	if (!u) return -1; // fatal
	if (!hse_fq) hse_fq = 64000;
	init_usart(0,u,1000*hse_fq/baud,!sync); 
	xprintf("[%d] serial %d is re-set src=%dkHz warn=%d sync=%d\n",
			get_ms_precise(),uart,hse_fq,warn,sync);
	return 0;
}

DECLARE_MOD(mp1_console) {
	.name = "mp1,console", .run = run_console
};
