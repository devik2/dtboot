#include <stdint.h>
#include <string.h>
#include "uimage.h"
#include "mtd.h"
#include "system.h"
#include "xprintf.h"

static uint32_t be2host(__be32 x)
{
	return __builtin_bswap32(x);
}

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
	if (!dst) dst = (void*)h.ih_load;
	h.ih_name[16] = 0; // short terminate string
	xprintf("[%d] image '%s', size 0x%x, dcrc 0x%08X, laddr 0x%X ep 0x%X\n",
			get_ms_precise(),h.ih_name,h.ih_size,
			h.ih_dcrc,dst,h.ih_ep);
	if (hdr) memcpy(hdr,&h,sizeof(h));

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

