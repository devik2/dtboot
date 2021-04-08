#include <string.h>
#include <stdarg.h>
#include "stm32mp1.h"
#include "xprintf.h"
#include "coro.h"
#include "gpio.h"
#include "fdt.h"
#include "stm32mp157axx_cm4.h"

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
} static const mp1_uart_txs[] = {
	{ PM_A(0),4,8 }, { PM_A(2),2,7 }, { PM_A(9),1,7 }, { PM_A(12),4,6 },
	{ PM_A(13),4,8 }, { PM_A(15),7,13 }, { PM_B(4),7,13 }, { PM_B(6),1,7 },
	{ PM_B(6),5,12 }, { PM_B(9),4,8 }, { PM_B(10),3,7 }, { PM_B(13),5,14 },
	{ PM_B(14),1,4 }, { PM_C(6),6,7 }, { PM_C(8),4,6 }, { PM_C(10),3,7 },
	{ PM_C(10),4,8 }, { PM_C(12),5,8 }, { PM_D(1),4,8 }, { PM_D(5),2,7 },
	{ PM_D(8),3,7 }, { PM_E(1),8,8 }, { PM_E(8),7,7 }, { PM_F(5),2,7 },
	{ PM_F(7),7,7 }, { PM_G(11),1,4 }, { PM_G(11),4,6 }, { PM_G(14),6,7 },
	{ PM_H(13),4,8 }, { PM_Z(2),1,7 }, { PM_Z(7),1,7 }
};

// sets given TX line up and returns UART id (0 for error)
// if up=0, pin is de-setup (set as analog)
uint32_t mp1_uart_tx_setup(uint32_t seq,int up)
{
	if (seq>=sizeof(mp1_uart_txs)/sizeof(struct mp1_uart_txs_t))
		return 0;

	const struct mp1_uart_txs_t *u = mp1_uart_txs + seq;
	gpio_setup_one(u->pin,up ? PM_OUT|PM_ALT:PM_AN,u->conf);
	return u->uart;
}

struct mp1_usart_info_t {
	uint32_t mem_off;
	uint16_t rcc_en_off;
	uint16_t rcc_ck_src;
	uint8_t rcc_en_bit;
	uint8_t rcc_ck_map;
} static const mp1_usart_info[] = {
	{ 0x5C000000,0x208,0xC8,4,0x25 },
	{ 0x4000E000,0xA00,0x8E8,14,0x24 },
	{ 0x4000F000,0xA00,0x8EC,15,0x24 },
	{ 0x40010000,0xA00,0x8E8,16,0x24 },
	{ 0x40011000,0xA00,0x8EC,17,0x24 },
	{ 0x44003000,0xA08,0x8E4,13,0x24 },
	{ 0x40018000,0xA00,0x8F0,18,0x24 },
	{ 0x40019000,0xA00,0x8F0,19,0x24 }
};

// returns USART pointer and sets its clock to clk (0=HSI,1=HSE)
USART_TypeDef *mp1_get_uart(uint32_t uid,uint32_t clk)
{
	if (!uid || uid>8) return NULL;
	const struct mp1_usart_info_t *u = mp1_usart_info + uid-1;
	volatile uint32_t *rcc = (void*)0x50000000;
	rcc[u->rcc_ck_src/4] = (u->rcc_ck_map >> (clk?0:4)) & 0xf;
	rcc[u->rcc_en_off/4] = 1<<u->rcc_en_bit;
	return (USART_TypeDef*)u->mem_off;
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
