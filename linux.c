#include <string.h>
#include "fdt.h"
#include "uimage.h"
#include "system.h"
#include "xprintf.h"
#include "gpio.h"
#include "stm32mp1.h"

__attribute__((noinline))
static void cache_clean(uint32_t dst,int32_t bytes)
{
	for (;bytes>0;bytes-=64,dst += 64) asm volatile(
                "dc  cvac,%0"::"r"(dst) );
}

static void start_linux(uint32_t start,uint32_t dtb)
{
	xprintf("[%d] starting Linux kernel at %X dtb %X\n",
			get_ms_precise(),start,dtb);
        __asm__(
		"IC IALLU\n"	// Invalidate instruction cache
		"DSB ISH\n"	// Ensure completion of the invalidations
		"isb\n"
        	"mrs x0, SCTLR_EL1\n"
		"mov x1, #0x5\n" // MMU off
        	"bic x0, x0, x1\n"
        	"msr SCTLR_EL1, x0\n"
		"isb\n"
                "mov x0,%0\n"
                "mov x1,#0\n"
                "mov x2,#0\n"
                "mov x3,#0\n"
                "mov x4,%1\n"
                "br  x4\n.ltorg"::"r"(dtb),"r"(start):"x1","x2","x3" );
}

void linux_enter_hook();
static int run_ldlinux(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	uint32_t kernel_img = 0x100000;
	uint32_t ramfs_img  = 0, *ptr;
	image_header_t ram_hdr, kern_hdr;

	gpio_setup_one(PM_Z(4),PM_ALT|PM_IN,6); // TODO: console

	if (!mctx->fdt || !mctx->mtd) return -4;

	if (lookup_fdt(mctx->fdt,"linux-img-start",&ptr,root) == 4) 
		kernel_img = be_cpu(*ptr);
	if (lookup_fdt(mctx->fdt,"linux-rd-start",&ptr,root) == 4) 
		ramfs_img = be_cpu(*ptr);

	uint32_t kernel = prep_uimage(mctx->mtd,kernel_img,&kern_hdr,PUF_CRC,NULL);
	if (kernel == -1) return -1;

	plat_patch_linux_dtb(fdt,root);

	// copy DTB to DDR
	memcpy(mctx->fdt_ddr,mctx->fdt,mctx->fdt_size);

	uint32_t ramfs = 0;
	if (ramfs_img)
		ramfs = prep_uimage(mctx->mtd,ramfs_img,&ram_hdr,PUF_CRC,NULL);
	if (ramfs == -1) return -2;

	uint32_t *chosen = lookup_fdt_section(mctx->fdt_ddr,"/chosen",NULL);
	if (chosen && ramfs) {
		uint32_t *a,*b;
		if (lookup_fdt(mctx->fdt_ddr,"linux,initrd-start",&a,chosen) == 4 &&
			  lookup_fdt(mctx->fdt_ddr,"linux,initrd-end",&b,chosen) == 4) {
			*a = be_cpu(ram_hdr.ih_load);
			*b = be_cpu(ram_hdr.ih_load+ram_hdr.ih_size);
			xprintf("fixed initrd addr to %X\n",ram_hdr.ih_load);
		}
	}
	console_stop_buf();
	cache_clean(kern_hdr.ih_load,kern_hdr.ih_size);
	linux_enter_hook();
	start_linux(kernel,(int)mctx->fdt_ddr);
	return 0;
}

DECLARE_MOD(mp1_ldlinux) {
	.name = "cob,ldlinux", .run = run_ldlinux
};
