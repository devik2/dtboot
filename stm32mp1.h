#ifndef __STM32MP1_H
#define __STM32MP1_H 1

#include <stdint.h>
#include "boot_api.h"
#include "stm32mp157axx_cm4.h"
#include "system.h"

#define SMC_SET_FREQ  0x83000101
#define SMC_DUMP_INFO 0x83000102
#define SMC_NSEC 0x83000103
uint32_t call_smc(uint32_t cmd,uint32_t a1,uint32_t a2);
	
#define NSEC 1
uint64_t get_arm_time();
uint32_t get_arm_tfreq();
extern uint8_t ms_freq_sh;
int ddr_init(int use_slow,int use_ddr3,int fast_test);
int set_pll(int n,int fi,int m,int mul,int dp,int dq,int dr);
void stgen_setup(int mhz,int use_hse,int keep);
void set_tz_sec();
void somp1_disable_osc();

void i2c_setup(I2C_TypeDef *I2C);
int i2c_wr(I2C_TypeDef *I2C,int addr,const char *data,int len,int aut);
int i2c_rd(I2C_TypeDef *I2C,int addr,char *buf,int len);
const char *mp1_get_rpn_name();

// Boot flags: each can have value 0..15 or undefined (-1)
// these can be hardcoded in bootloader binary or overriden
// by TEMP registers.
// Each flag is encoded as 8 bits: MSB:{BFI} LSB:{VAL} and
// last one is always checksum (BFI_CHECK).
#define BOOT_MODE_INSN 17	// in TEMP; set by linux to alter next boot
#define BOOT_FLAGS_POS 18	// in TEMP
#define BOOT_FLAGS_MAX 1	// in TEMP
#define BFI_CHECK 0	// last one in block, value is XOR of all prev nibbles
#define BFI_UARTL 1	// uart id (see mp1_uart_txs) 0..15
#define BFI_UARTH 2	// uart id 16..31
#define BFI_NONS 3	// nonzero if to stay in secure mode
#define BFI_DTB_ID 4	// which DTB to use (0=default)
#define BFI_SHELL 5	// run debug shell if >0
#define BFI_MACH1 6	// private use by machines
#define BFI_MACH2 7	// private use by machines

// subclass machine context
struct stm32mp1_mctx {
	struct machine_ctx_t ctx;	// parent
	const boot_api_context_t *bctx; // given by ROM bootloader
	int8_t boot_flags[16];		// 0xff (-1) for unset flag
	uint8_t lump_rev;	// 0=orig,1=with stm8,2=exmp1
	uint8_t boot_insn;	// extra boot request from linux
};

// load and merge flags from boot header and TEMP to context
// boot_flags member
void mp1_read_boot_flags(struct stm32mp1_mctx *ctx);
void mp1_show_boot_flags();
void mp1_set_boot_insn(uint8_t insn);
uint32_t mp1_uart_tx_setup(uint32_t seq,int up);
USART_TypeDef *mp1_get_uart(uint32_t uid,uint32_t clk);
void init_usart(int id,USART_TypeDef *usart,int div,int buf);

#endif
