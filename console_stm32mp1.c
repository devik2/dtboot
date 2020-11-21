#include <string.h>
#include <stdarg.h>
#include "stm32mp1.h"
#include "xprintf.h"
#include "coro.h"
#include "gpio.h"
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

int cons_task;
static void init_usart_once()
{
	static char inited;
	if (inited) return;
	inited = 1;
	cons_do_buf = 0;
	if (cons_do_buf) {
		xstream_out.putc = uart_putc_buf;
		cons_task = coro_new(uart_thread,NULL,0,"console");
	} else 
		xstream_out.putc = uart_putc;
}

void console_sync()
{
	if (!cons_do_buf) return;
	//uint8_t sync_pt = utx_wr;
	while (utx_wr != utx_rd) coro_yield();
}

// used when coroutines support ends (when starting Linux)
void console_stop_buf()
{
	if (!cons_task) return;
	console_sync();
	coro_kill(cons_task); cons_task = 0;
	cons_do_buf = 0;
	xstream_out.putc = uart_putc;
	xprintf("Console thread stopped\n");
}

void init_usart(int id,USART_TypeDef *usart,int div)
{
	usart->BRR = div;
	usart->CR1 = USART_CR1_UE|USART_CR1_TE|USART_CR1_FIFOEN;
	dusart[id&1] = usart;
	init_usart_once();
}

static char buff[256];
void cons_aux(const char *fmt,...)
{
	va_list arp;
	xstream_t f;
	f.ptr = buff;
	f.space = sizeof(buff)-1;
	va_start(arp, fmt);
	xvfprintf(&f, fmt, arp);
	va_end(arp);
	if (f.space>0) *f.ptr = 0;	/* Terminate output string with a \0 */

	const char *p = buff;
	for (;*p;p++) {
		while (!(USART3->ISR & USART_ISR_TXE_TXFNF));
		USART3->TDR = *p;
	}
}

void cons_init_U3()
{
	trace(1,0,0);
	RCC->MP_APB1ENSETR = RCC_MC_APB1ENSETR_USART3EN;
	gpio_setup_one(PM_B(10),PM_ALT,7);
	USART3->BRR = 24000000/115200;
	USART3->CR1 = USART_CR1_UE|USART_CR1_TE|USART_CR1_FIFOEN;
	cons_aux("AUX inited\n");
}
