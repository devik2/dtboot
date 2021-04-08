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
	int i;
	const struct mtd_chip_t *chip = prog_dev->chip;
	int pg1 = prog_a >> chip->page_sh, pg2 = (prog_a+prog_l-1) >> chip->page_sh;
	const char *pa = (char*)prog_s;
	int sz = 1 << chip->page_sh;
	for (i=pg1; i<=pg2; i++) {
		prog_dev->read_page(prog_dev,i,mtd_buf,sz);
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

#define STORE_BBT 0

#if STORE_BBT
static int write_bbt(int eb,int second,const uint16_t *bbt,int bsz)
{
	const struct mtd_chip_t *chip = prog_dev->chip;
	int esh = chip->eb_sh;
	int dsz = 1 << chip->page_sh;
	int esz = 1 << chip->ecc_sh;
	memset(mtd_buf,0xff,dsz+esz); // mark all ok
	memcpy(mtd_buf+dsz+0xe,second?"1tbB":"Bbt0",5);
	int i;
	for (i=0;i<bsz;i++) {
		uint16_t idx = bbt[i];
		mtd_buf[idx>>2] &= ~(3<<((idx&3)*2));
	}
	i = chip->eb_cnt;
	mtd_buf[i>>2] = 0xaa;	// last 4 (BBT) marked as reserved

	if ((i=prog_dev->erase(prog_dev,eb<<esh))<0) {
		xprintf("error erasing BBT EB %d (err %d)\n",eb,i);
		return i;
	}
	if ((i=prog_dev->write_page(prog_dev,eb<<esh,mtd_buf,dsz+esz))<0) {
		xprintf("error writing BBT EB %d (err %d)\n",eb,i);
		return i;
	}
	xprintf("BBT sec=%d written to EB %d\n",second,eb);
	return 0;
}
#endif

#define MAX_BB 64
static void scan_bb()
{
	const struct mtd_chip_t *chip = prog_dev->chip;
	if (!MTD_ISNAND(chip)) {
		xprintf("must be NAND!\n"); return;
	}
	uint16_t bbs[MAX_BB];
	xprintf("bad block scan, cnt=%d\n",chip->eb_cnt);
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
		if (ec>=MAX_BB) {
			xprintf("too many BBs found, ignore others!\n"); 
			break;
		}
		bbs[ec++] = i;
	}
	xprintf("found %d BBs:",ec);
	for (i=0;i<ec;i++) xprintf("%d%c",bbs[i],i==ec-1?'\n':',');
	if (ec && bbs[ec-1]>=chip->eb_cnt-4) {
		xprintf("BB in BBT area, not supported yet!\n");
		return;
	}
#if STORE_BBT
	write_bbt(chip->eb_cnt-2,0,bbs,ec);
	write_bbt(chip->eb_cnt-1,1,bbs,ec);
#endif
}

#if STORE_BBT
static void dump_bbt()
{
	const struct mtd_chip_t *chip = prog_dev->chip;
	int dsz = 1 << chip->page_sh;
	int esz = 1 << chip->ecc_sh;
	if (!MTD_ISNAND(chip)) {
		xprintf("must be NAND!\n"); return;
	}
	if (prog_a>=chip->eb_cnt) return;
	int cnt,j,i = prog_dev->read_page(
			prog_dev,prog_a<<chip->eb_sh,mtd_buf,dsz+esz);
	if (i<0) return;
	const char *tab = NULL;
	if (!memcmp(mtd_buf+dsz+0xe,"Bbt0",4)) tab = "primary";
	if (!memcmp(mtd_buf+dsz+0xe,"1tbB",4)) tab = "secondary";
	xprintf("at %d found %s BBT version 0x%02X\n",prog_a,
			tab ? tab : "no",mtd_buf[dsz+0x12]);
	if (!tab) return;
	for (i=0,cnt=0;i<dsz;i++) {
		if (mtd_buf[i]==0xff) continue;
		for (j=0;j<4;j++) {
			int c = (mtd_buf[i] >> (j*2)) & 3;
			if (c==3) continue;
			xprintf("%2d. BB at %d: %d\n",++cnt,i*4+j,c);
		}
	}
}
#endif

static void dump_page()
{
	mtd_dump_page(prog_dev,prog_a,0);
}

static void erase_eb() // use pagenumber as input !
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
#if STORE_BBT
	if (prog_s == 5) { dump_bbt(); goto pend; }
#endif
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
