#ifndef __CO_SYSTEM_H
#define __CO_SYSTEM_H
#include <stdint.h>

static inline int seq16_after(int16_t a,int16_t b)
{
	return (b-a)&0x8000;
}

static inline int seq64_after(int64_t a,int64_t b)
{
	return (b-a)<0;
}

static inline int seq_after(int a,int b)
{
	return (b-a)<0;
}

uint32_t get_ms();
void udelay(uint32_t us);
uint32_t get_ms_precise();
uint64_t get_us_precise();
void progress();
void console_sync();
void console_stop_buf();
void panic(const char *msg,...);
uint32_t crc32 (uint32_t crc, const void *p, uint32_t len);

struct module_inst_t;
struct boot_param_header;

// patch DTB before booting kernel by plat specific info
void plat_patch_linux_dtb(struct boot_param_header *fdt,uint32_t *root);

// state of machine currently booting, can be subclassed
// by actual machine implmentation code
struct machine_ctx_t {
	struct boot_param_header *fdt;	// where the FDT is (for use)
	struct boot_param_header *fdt_ddr;// where the FDT should be for boot
	uint32_t fdt_size;
	struct mtd_dev_t *mtd;		// boot device
	uint16_t ddr_mb;
	uint16_t _pad;
} extern *mctx;

struct module_desc_t {
	const char *name;	// used to match DT
	int (*run)(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root);	// can return nonzero as error
};// extern *mach; XXX

#if 0
struct module_inst_t {
	struct module_desc_t *dsc;
	struct boot_param_header *fdt;
	uint32_t *root;		// root of this instance config
	void *priv;		// module private data
	uint8_t cid;		// coroutine id (0=no coro)
};
#endif
void run_modules(struct boot_param_header *fdt,uint32_t *root);

#define DECLARE_MOD(CB) struct module_desc_t _mach_##CB __attribute__((section(".modtab"))) =
#endif
