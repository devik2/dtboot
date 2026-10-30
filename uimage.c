#include <stdint.h>
#include <string.h>
#include "uimage.h"
#include "mtd.h"
#include "system.h"
#include "xprintf.h"
#include "minilzo.h"

static uint32_t be2host(__be32 x)
{
	return __builtin_bswap32(x);
}

// parse LZOP header, returns its size or <0 (error)
static int32_t lzop_header(const uint8_t *d,uint32_t *flags)
{
	const uint8_t magic[9] = { 0x89, 0x4c, 0x5a, 0x4f, 0x00, 0x0d, 0x0a, 0x1a, 0x0a };
	*flags = 0;
	if (memcmp(d,magic,9)) return -1;
	d += 9;
	const uint8_t *h = d;
	uint16_t fver = (d[0]<<8)|d[1], need = 0, meth;
	//uint16_t lver = (d[2]<<8)|d[3];
	d += 4;
	if (fver >= 0x940) {
		need = (d[0]<<8)|d[1];
		meth = d[2];
		d += 3;
	} else meth = d[0];
	d++; // skip method
	if (fver < 0x900 || need > 0x1001 || meth < 1 || meth > 3) {
		_halt();
		return -2;
	}
	memcpy(flags,d,4); *flags = be2host(*flags);
	if ((*flags & 0x3ff3) != 0x1100) return -3;
	d += 16;
	d += d[0]+1; // skip filename
	uint32_t crc; memcpy(&crc,d,4); crc = be2host(crc);
	uint32_t hcrc = crc32(0,h,d-h);
	if (crc != hcrc) return -4;
	//xprintf("LZO CRC %X %X\n",crc,hcrc);
	return d-h+4+9;
}

static int32_t unpack_lzop2(struct mtd_dev_t *mtd,uint32_t addr,
		uint32_t off,void *dst)
{
	// LZO is in LZOP format with up to 256k blocks; use temp SRAM1/2 buffer
	//void *sbuf = (void*)0x20040000; // SRAM1 TODO
	void *sbuf = (void*)0x82000000; // SRAM1 TODO (sram is used by SCMI)

	int pgsh = mtd->chip->page_sh;
	int pgsz = 1 << pgsh;
	if (pgsz < 64+off) { // we expect LZOP header in the first page
		xprintf("bad MTD page size: %d\n",pgsz); 
		return -1;
	}
	// these variables always reflect sbuf content
	uint32_t page = addr >> pgsh;
	uint32_t pg_cnt = 1;
	int bb = mtd->read_seq(mtd,page,sbuf,pg_cnt,MTDRF_SKIPBB);
	if (bb<0) {
		xprintf("can't read LZOP hdr: %d\n",bb);
		return -1;
	}
	uint32_t flg;
	int32_t hsz = lzop_header(sbuf+off,&flg);
	if (hsz <= 0) {
		xprintf("Invalid LZO format (use LZOP+CRC), flg=0x%X err=0x%X\n",flg,hsz);
		return -1;
	}
	off += hsz; // off points to LZO datablock
	uint32_t zhdr[3]; // uncomp_sz,comp_sz,crc
	memcpy(zhdr,sbuf+off,sizeof(zhdr));

	while (zhdr[0]) {
		uint64_t t1 = get_ms();
		// there is valid zhdr to be loaded
		for (int i=0;i<3;i++) zhdr[i] = be2host(zhdr[i]);
		if (zhdr[0] > 0x40000 || zhdr[1] > 0x40000) {
			_halt();
			return -1;		// LZOP should emit max 256k blocks
		}
		// (re)load pages where source data resides
		page = (addr+off) >> pgsh;
		uint32_t pg_cnt = (off+2*sizeof(zhdr)+zhdr[1]+pgsz-1) >> pgsh;
		int r = mtd->read_seq(mtd,page+bb,sbuf,pg_cnt,MTDRF_SKIPBB);
		if (r < 0) return -1;
		bb += r;
		uint64_t t2 = get_ms();

		if (zhdr[0] == zhdr[1]) memcpy(dst,sbuf+off+sizeof(zhdr),zhdr[1]);
		else {
			lzo_uint szo = zhdr[0];
			r = lzo1x_decompress_safe(sbuf+off+sizeof(zhdr),zhdr[1],dst,&szo,NULL);
			if (r < 0) {
				xprintf("LZO dec err %d\n",r);
				_halt();
				wait_for_halt();
				return -1;
			}
		}
		uint64_t t3 = get_ms();
		//xprintf(" dec blk %X %X %X %X tm %d %d\n",addr,off,zhdr[0],zhdr[1],
		//		t2-t1,t3-t2);
		dst += zhdr[0];
		off += sizeof(zhdr)+zhdr[1];
		memcpy(zhdr,sbuf+off,sizeof(zhdr)); // we loaded next header at end
		// fix addr & off
		uint32_t pg_diff = off & ~(pgsz-1);
		off -= pg_diff;
		addr += pg_diff;
	}
	return 0;
}

extern int split_dma;
void wait_dma();
#if 0
// assume there is LZOP file at flash addr+off (addr is expected to be page aligned)
// and decompress it to dst
static int32_t unpack_lzop(struct mtd_dev_t *mtd,uint32_t addr,
		uint32_t off,void *dst)
{
	// LZO is in LZOP format with up to 256k blocks; use temp SRAM1/2 buffer
	void *sbuf = (void*)0x81800000; // SRAM1 TODO

	int pgsh = mtd->chip->page_sh;
	int pgsz = 1 << pgsh;
	if (pgsz < 64+off) { // we expect LZOP header in the first page
		xprintf("bad MTD page size: %d\n",pgsz); 
		return -1;
	}
	// these variables always reflect sbuf content
	uint32_t page = addr >> pgsh;
	int bb = mtd->read_page(mtd,page,sbuf,pgsz);
	if (bb<0) {
		xprintf("can't read LZOP hdr: %d\n",bb);
		return -1;
	}
	uint32_t flg;
	int32_t hsz = lzop_header(sbuf+off,&flg);
	if (hsz <= 0) {
		xprintf("Invalid LZO format (use LZOP+CRC), flg=0x%X err=0x%X\n",flg,hsz);
		return -1;
	}
	off += hsz; // off points to LZO datablock
	uint32_t zhdr[3]; // uncomp_sz,comp_sz,crc
	memcpy(zhdr,sbuf+off,sizeof(zhdr));

	while (zhdr[0]) {
		uint64_t t1 = get_ms();
		// there is valid zhdr to be loaded
		for (int i=0;i<3;i++) zhdr[i] = be2host(zhdr[i]);
		if (zhdr[0] > 0x40000 || zhdr[1] > 0x40000) {
			_halt();
			return -1;		// LZOP should emit max 256k blocks
		}
		// (re)load pages where source data resides
		page = (addr+off) >> pgsh;
		uint32_t end_pg = (off+2*sizeof(zhdr)+zhdr[1]+pgsz-1) >> pgsh;
		int r = mtd->read_page(mtd,page,sbuf,(zhdr[1]+1024)&0xffff00);
		if (r < 0) return -1;
		uint64_t t2 = get_ms();

		//xprintf(" dec blk %X %X %X %X at 0x%X\n",
		//		addr,off,zhdr[0],zhdr[1],sbuf+off+sizeof(zhdr));
		if (zhdr[0] == zhdr[1]) memcpy(dst,sbuf+off+sizeof(zhdr),zhdr[1]);
		else {
			lzo_uint szo = zhdr[0];
			r = lzo1x_decompress_safe(sbuf+off+sizeof(zhdr),zhdr[1],dst,&szo,NULL);
			if (r < 0) {
				xprintf("LZO dec err %d\n",r);
				return -1;
			}
		}
		uint64_t t3 = get_ms();
		//xprintf(" dec blk %X %X %X %X tm %d %d\n",addr,off,zhdr[0],zhdr[1],
		//		t2-t1,t3-t2);
		dst += zhdr[0];
		off += sizeof(zhdr)+zhdr[1];
		memcpy(zhdr,sbuf+off,sizeof(zhdr)); // we loaded next header at end
		// fix addr & off
		uint32_t pg_diff = off & ~(pgsz-1);
		off -= pg_diff;
		addr += pg_diff;
	}
	return 0;
}
#endif
// returns start address, -1 if error
// if action bit 0 set, copy image to its load address
uint32_t prep_uimage(struct mtd_dev_t *mtd,uint32_t addr,
		struct image_header *hdr,int flags,void *dst)
{
	int i;
	struct image_header h;
	uint8_t *pbuf = mtd_buf;
	uint32_t *p = (void*)&h;
	int pgsz = 1 << mtd->chip->page_sh;
	if (pgsz > MTD_BUFSZ || pgsz < sizeof(h)) {
		xprintf("bad MTD page size: %d\n",pgsz); 
		return -1;
	}
	uint32_t page = addr >> mtd->chip->page_sh;
	// read first page (uimage header)
	int bb = mtd->read_seq(mtd,page,pbuf,1,MTDRF_SKIPBB);
	if (bb<0) {
		xprintf("can't read uimage hdr: %d",bb);
		return -1;
	}
	memcpy (&h,pbuf,sizeof(h));
	if (be2host(h.ih_magic) != IH_MAGIC) {
		xprintf("bad uimage header, addr %X, page %X\n",addr,page); 
		dump_mem((void*)&h,sizeof(h));
		return -1;
	}
	uint32_t hcrc = be2host(h.ih_hcrc);
	h.ih_hcrc = 0;
	uint32_t acrc = crc32(0,&h,sizeof(h));
	if (acrc != hcrc) {
		xprintf("bad header CRC (%X should be %X)\n",acrc,hcrc); 
		return -1;
	}
	for (i=0;i<7;i++) p[i] = be2host(p[i]);
	if (!dst) dst = (void*)(uint64_t)h.ih_load;
	h.ih_name[16] = 0; // short terminate string
	xprintf("[%d] image '%s', size 0x%x, dcrc 0x%08X, laddr 0x%X ep 0x%X\n",
			get_ms_precise(),h.ih_name,h.ih_size,
			h.ih_dcrc,dst,h.ih_ep);
	if (hdr) memcpy(hdr,&h,sizeof(h));

	if (h.ih_comp == IH_COMP_LZO) {
		//int32_t rv = unpack_lzop(mtd,addr,sizeof(h),dst);
		int32_t rv = unpack_lzop2(mtd,addr,sizeof(h),dst);
		if (rv < 0) return -1;
		acrc = crc32(0,dst,h.ih_size);
		xprintf("[%d] uncompr. data CRC %X sz %d\n",get_ms_precise(),acrc,h.ih_size);

		return h.ih_ep;
	}
	// copy part already in page buffer
	memcpy(dst,pbuf+sizeof(h),pgsz-sizeof(h));

	// copy remainder from MTD
	i = (h.ih_size+sizeof(h)+pgsz-1) >> mtd->chip->page_sh;
	bb = mtd->read_seq(mtd,page+1+bb,dst+pgsz-sizeof(h),i-1,MTDRF_SKIPBB);
	if (flags & PUF_CRC) {
		xprintf("[%d] check data CRC at %X sz %d\n",get_ms_precise(),
				dst,h.ih_size);
		acrc = crc32(0,dst,h.ih_size);
		i = acrc == h.ih_dcrc;
		xprintf("[%d] CRC is %s\n",get_ms_precise(),i?"OK":"BAD");
		if (!i) return -1;
	}
	return h.ih_ep;
}

