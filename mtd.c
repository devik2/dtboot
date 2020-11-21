#include <string.h>
#include "system.h"
#include "mtd.h"
#include "fdt.h"
#include "coro.h"
#include "xprintf.h"
/* BB TC$:BB at EB 186 (0 0)                             
BB at EB 1326 (0 0)                                             
BB at EB 1328 (0 0)                                      
BB at EB 1536 (0 0)                                    
BB at EB 1537 (0 0)
 */

// MP1 CFG9, TC58: 0xA0420000 1 01 0|0 000|0100|0 0 10|0 (4k/64/2048)
// MP1 CFG9, W25N: 0x80220000 1 00 0|0 000|0010|0 0 10|0 (2k/64/1024)

static struct mtd_chip_t fchips[] = {
	// flags,pgsh,ebsh,,wr_sh,eccsh,ebcnt,id
	{ MTDF_NAND|MTDF_CONT,11,6,0,11,6,1024,0xaaef,0xffff,"W25N01GV" },
	{ MTDF_NAND,12,6,0,12,7,2048,0xed98,0xffff,"TC58CVG2S0HRAIJ" },
	{ MTDF_NAND|MTDF_RDRAND,11,6,0,11,7,1024,0x142C,0xffff,"MT29F1G01_3V3" },
	{ MTDF_NAND|MTDF_RDRAND|MTDF_2PL,
		11,6,0,11,7,2048,0x242C,0xffff,"MT29F2G01" },
	{ 0,8,8,0,8,0,1024,0x199d,0xffff,"IS25[LW]P512M" },
	{ 0,9,9,0,9,0,256,0x1901,0xffff,"S25FL512S" },
	{ MTDF_DYN, },
	{ MTDF_DYN, },
	{ 0,0,  }
};

static struct mtd_chip_t *mtd_lookup_free_dyn()
{
	struct mtd_chip_t *chip = fchips;
	for (;chip->name;chip++)
	       if (chip->flags & MTDF_DYN && !chip->page_sh) return chip;
	return NULL;
}

// autodetect by parameter page TODO
static int mtd_autodetect_chip(struct mtd_dev_t *dev)
{
}

const struct mtd_chip_t *mtd_lookup_flash(uint32_t id)
{
	const struct mtd_chip_t *chip = fchips;
	for (;chip->name;chip++)
	       if ((id & chip->id_mask) == chip->id && chip->page_sh) return chip;
	return NULL;
}

// page read random usage
static int mtd_rand_read_seq(struct mtd_dev_t *dev,uint32_t page,
		uint8_t *buf, int pg_cnt,int flags)
{
}

int mtd_generic_read_seq(struct mtd_dev_t *dev,uint32_t page,uint8_t *buf,
		int pg_cnt,int flags)
{
//	if (pg_cnt>=10 && dev->chip->flags & MTD_CFL_RDRAND)
//		return mtd_rand_read_seq(dev,page,buf,pg_cnt,flags);
	int i, sz = 1 << dev->chip->page_sh, errs = 0, bb = 0;
	for (i=0;i<pg_cnt;i++) {
		int sts = dev->read_page(dev,page+i,buf,sz);
		//int sts = dev->read_page(dev,page+i,buf,pg_cnt<2?sz:1);
		if (sts<0 && (flags & MTDRF_SKIPBB)) {
			pg_cnt++;
			if (++bb > 5) return -3;
			continue;
		}
		if (sts<0) return sts;
		errs += sts;
		buf += sz;
	}
	return flags & MTDRF_SKIPBB ? bb : errs;
}

// debug functions

char mtd_buf[MTD_BUFSZ] __attribute__((aligned(4)));

void dump_mem(const uint8_t *p,int sz)
{
	int i;
	for (i=0;i<sz;i++) {
		if ((i & 15)==0) xprintf("%03X:",i);
		xprintf(" %02X",p[i]);
		if ((i & 15)==15) xprintf("\n");
	}
	if ((i & 15)!=0) xprintf("\n");
}

void mtd_dump_page(struct mtd_dev_t *dev,uint32_t p,int msz)
{
	const struct mtd_chip_t *chip = dev->chip;
	int sz = (1 << chip->page_sh) + (1 << chip->ecc_sh),i;
	unsigned totp = chip->eb_cnt<<chip->eb_sh;
	if (msz && msz<sz) sz = msz;
	if (sz > MTD_BUFSZ) {
		xprintf("small MTD_BUFSZ %d<%d\n",MTD_BUFSZ,sz);
		return;
	}
	if (p > totp) {
		xprintf("invalid page %d (total %d)\n",p,totp);
		return;
	}
	memset(mtd_buf,0,sz);
	i = dev->read_page(dev,p,mtd_buf,sz);
	xprintf("MTD %s page 0x%X/0x%X sts %d:\n",chip->name,p,totp,i);
	dump_mem(mtd_buf,sz);
}

static int fix_part_size(struct module_desc_t *dsc,struct boot_param_header *fdt,
		uint32_t *root)
{
	const struct mtd_chip_t *chip = mctx->mtd->chip;
	int mtd_sz = chip->eb_cnt << (chip->eb_sh+chip->page_sh);
	uint32_t ph = 0;
	fetch_fdt_ints(fdt,root,"patch-size-to",1,1,&ph);
	if (ph) do {
		uint32_t *p,*hdl = lookup_fdt_by_phandle(fdt,ph,NULL);
		if (!hdl) break;
		if (lookup_fdt(fdt,"reg",&p,hdl)!=8) break;
		mtd_sz -= be_cpu(p[0]);	// start of the partition
		mtd_sz -= 8 << (chip->eb_sh+chip->page_sh); // reserve 8 EBs
		xprintf("patching part mtd size (%dMB)\n",mtd_sz>>20);
		if (mtd_sz>be_cpu(p[1])) p[1] = be_cpu(mtd_sz);
	} while(0);
	return 0;
}

DECLARE_MOD(mp1_fix_part_size) {
	.name = "cob,mtdfix", .run = fix_part_size
};
