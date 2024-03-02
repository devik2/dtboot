#include <stdint.h>

#define MTDF_TMASK  0x03	// type mask, types:
#define MTDF_NOR    0x00
#define MTDF_NAND   0x01
#define MTD_ISNAND(CH) (((CH)->flags & MTDF_TMASK) == MTDF_NAND)

#define MTDF_RDRAND 0x10	// Micron style read random supported
#define MTDF_2PL 0x20		// dual plane
#define MTDF_DYN 0x80		// dynamic record
struct mtd_dev_t;
struct mtd_chip_t {
	uint8_t flags;		// MTDF_XXX
	uint8_t page_sh;	// page size shift
	uint8_t eb_sh;		// erase block size shift in pages
	uint8_t _pad;
	uint8_t wr_sh;		// write size shift (bytes)
	uint8_t ecc_sh;		// bytes shift in spare blk
	uint16_t eb_cnt;	// total erase blocks
	uint32_t id;
	uint32_t id_mask;
	const char *name;	// name of chip type
	int (*init_fn)(struct mtd_dev_t *dev);
};

#define MTDRF_SKIPBB 1	// read only good blks, return no of skipped blks
struct mtd_dev_t {
	const struct mtd_chip_t *chip;
	const char *drv_name;
	uint32_t drv_priv[4];
	uint32_t ecc_errs;
	// read/write page into buffer, sz can be 1..page_size
	// address & sz must be 32bit aligned
	int (*read_page)(struct mtd_dev_t *dev,uint32_t page,uint8_t *buf,int sz);
	int (*write_page)(struct mtd_dev_t *dev,uint32_t page,const uint8_t *buf,int sz);
	int (*erase)(struct mtd_dev_t *dev,uint32_t page);	// erase block of page
	// read sequence of pages, can be optimized by driver
	int (*read_seq)(struct mtd_dev_t *dev,uint32_t page,uint8_t *buf,
			int pg_cnt,int flags); // MTDRF_XXX
};

const struct mtd_chip_t *mtd_lookup_flash(uint32_t id);
void mtd_dump_page(struct mtd_dev_t *dev,uint32_t p,int sz);
void dump_mem(const uint8_t *p,int sz);
int mtd_nand_get_feat(unsigned addr);
int mtd_nand_set_feat(unsigned addr,unsigned val);

// detects chip on QSPI bus id and fills dev struct, return 0 if ok
// nonzero flags means to use slow clock
int mtd_detect_qspi(struct mtd_dev_t *dev,int bus,int flags);
void qspi_set_divider(int div);

#define MTD_BUFSZ (4096+256)
extern uint8_t mtd_buf[MTD_BUFSZ];
