#include <string.h>
#include "system.h"
#include "mtd.h"
#include "xprintf.h"

extern uint32_t prog_a,prog_s,prog_l;
struct mtd_dev_t *prog_dev;	// should be set by loader

static int mem_is_not_ff(const uint8_t *p,int sz)
{
	for (;sz>0;sz--,p++) if (*p != 0xff) return -1;
	return 0;
}

static int verify(int data)
{
	int i,e;
	const struct mtd_chip_t *chip = prog_dev->chip;
	int pg1 = prog_a >> chip->page_sh, pg2 = (prog_a+prog_l-1) >> chip->page_sh;
	const char *pa = (char*)prog_s;
	int sz = 1 << chip->page_sh;
	for (i=pg1; i<=pg2; i++) {
		e = prog_dev->read_page(prog_dev,i,mtd_buf,sz);
		int diff = data ? memcmp(mtd_buf,pa,sz) : mem_is_not_ff(mtd_buf,sz);
		if (diff) {
			xprintf("verify(%d) error at addr 0x%X\n",
					data,i<<chip->page_sh);
			console_sync();
			return -1;
		}
		pa += sz;
	}
	return 0;
}

static void scan_bb()
{
	const struct mtd_chip_t *chip = prog_dev->chip;
	if (!MTD_ISNAND(chip)) {
		xprintf("must be NAND!\n"); return;
	}
	char buf[512];
	xprintf("bad block scan, cnt=%d\n",chip->eb_cnt);
	if (sizeof(buf)*8 < chip->eb_cnt) {
		xprintf("small BB buf!\n"); return;
	}
	memset(buf,0,sizeof(buf));
	int i,e,ec=0;
	int sz = (1 << chip->page_sh) + (1 << chip->ecc_sh);
	uint64_t *ecc_ptr = (uint64_t*)(mtd_buf+(1 << chip->page_sh));
	for (i=0;i<chip->eb_cnt;i++) {
		memset(mtd_buf,0,sz);
		e = prog_dev->read_page(prog_dev,i<<chip->eb_sh,mtd_buf,sz);
		if (e<0) {
			xprintf("cant read eb %d\n",i);
		} 
		else if (*ecc_ptr == -1LL) continue;
		xprintf("BB at EB %d (%X %X)\n",i,
				*(int*)ecc_ptr,((int*)ecc_ptr)[1]);
		buf[i>>3] |= 1<<(i&7); ec++;
	}
	xprintf("found %d BBs\n",ec);
}

static void dump_page()
{
	mtd_dump_page(prog_dev,prog_a,0);
}

static void erase_eb()
{
	int e;
	if ((e=prog_dev->erase(prog_dev,prog_a))<0) {
		xprintf("error erasing %X: %d\n",prog_a,e);
	}
}

static void make_bb()
{
	const struct mtd_chip_t *chip = prog_dev->chip;
	if (!MTD_ISNAND(chip)) {
		xprintf("must be NAND!\n"); return;
	}
	int sz = (1 << chip->page_sh) + (1 << chip->ecc_sh);
	memset(mtd_buf,0xff,sz);
	mtd_buf[1 << chip->page_sh] = 0; // marker
	int e = prog_dev->write_page(prog_dev,prog_a,mtd_buf,sz);
	if (e<0) {
		xprintf("error write %X: %d\n",prog_a,e);
	}
}

void prog_ext()
{
	if (!prog_dev) {
		xprintf("mtd_prog dev not set!\n");
		console_sync();
		goto pend;
	}
	if (prog_s == 1) { scan_bb(); goto pend; }
	if (prog_s == 2) { dump_page(); goto pend; }
	if (prog_s == 3) { erase_eb(); goto pend; }
	if (prog_s == 4) { make_bb(); goto pend; }

	const struct mtd_chip_t *chip = prog_dev->chip;
	xprintf("ext program from %X to %s at %X sz %d\n",
			prog_s,chip->name,prog_a,prog_l);

	int bk_sh = chip->eb_sh+chip->page_sh;
	int bk_sz = 1<<bk_sh;
	int i,e;
	if (prog_a & (bk_sz-1)) {
		xprintf("unaligned addr: %X\n",prog_a);
		goto pend;
	}
	int sect1 = prog_a >> bk_sh, sect2 = (prog_a+prog_l-1) >> bk_sh;
	if (sect2 >= chip->eb_cnt) {
		xprintf("write beyond end, sect: %X\n",sect2);
		goto pend;
	}
	for (i=sect1;1 && i<=sect2; i++) {
		xprintf("erase sect %X\n",i);
		if ((e=prog_dev->erase(prog_dev,i << chip->eb_sh))<0) {
			xprintf("error erasing %X: %d\n",i,e);
			goto pend;
		}
	}
	int pg1 = prog_a >> chip->wr_sh, pg2 = (prog_a+prog_l-1) >> chip->wr_sh;
	verify(0);
	const char *pa = (char*)prog_s;
	for (i=pg1; i<=pg2 && 1; i++) {
		if ((i&255)==0 || i==pg2 || i==pg1) xprintf("prog subpage %X\n",i);
		e = prog_dev->write_page(prog_dev,i,pa,1 << chip->wr_sh);
		if (e<0) {
			xprintf("error write %X: %d\n",i,e);
			goto pend;
		}
		pa += 1 << chip->wr_sh;
	}
	verify(1);
	xprintf("verify ok.\n");
	//mtd_dump_page(prog_dev,128,128);
pend:
	xprintf("done.\n");
	prog_l = 0;
	console_sync();
	asm("bkpt");
}
