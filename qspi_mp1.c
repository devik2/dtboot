#include <string.h>
#include "stm32mp1.h"
#include "system.h"
#include "mtd.h"
#include "coro.h"
#include "xprintf.h"
#include "gpio.h"

#define QMO_RD  1
#define QMO_WR  2
#define QMO_DBL  4
#define QMO_QDL  8
#define QAD(n)  (n<<4) // address bytes 0..4
#define QNOINS  0x80
#define QDMY  0x100
#define QDMA  0x200

// 0x40430000
#define QUADSPI OCTOSPI1

void qspi_set_divider(int div)
{
//	div = 1; // TODO
	xprintf("QSPI: set divider %d\n",div);
	QUADSPI->DCR2 = div-1;
}

#define DMA_BLKSZ 16
static void qspi_init(int lines)
{
	rcc_en_clock(&RCC->OSPI1CFGR);
	rcc_en_clock(&RCC->OSPIIOMCFGR);
	rcc_en_clock(&RCC->HPDMA1CFGR);
	rcc_flx_chan_set(48,5,1,1); // HSI as kernel clk

	QUADSPI->CR = 0;
	udelay(100);
	QUADSPI->DCR1 = (29 << OCTOSPI_DCR1_DEVSIZE_Pos) // 1GB size
		|OCTOSPI_DCR1_DLYBYP|OCTOSPI_DCR1_MTYP_1;
	QUADSPI->CR = OCTOSPI_CR_EN|((DMA_BLKSZ-1)<<OCTOSPI_CR_FTHRES_Pos);

	struct gpio_setup_t qpins[] = {
		{ PM_D(0), PM_ALT|PM_SPD(2), 10, }, // CLK
		{ PM_D(3), PM_ALT|PM_SPD(2), 10, }, // NCS
		{ PM_D(4), PM_ALT|PM_SPD(2), 10, }, // IO0
		{ PM_D(5),PM_ALT|PM_SPD(2), 10, }, // IO1
		{ PM_D(6),PM_ALT|PM_SPD(2), 10, },
		{ PM_D(7),PM_ALT|PM_SPD(2), 10, },
#if 0
		{ PM_D(8),PM_ALT|PM_SPD(2), 10, },
		{ PM_D(9),PM_ALT|PM_SPD(2), 10, },
		{ PM_D(10),PM_ALT|PM_SPD(2), 10, },
		{ PM_D(11),PM_ALT|PM_SPD(2), 10, },
#endif
		{0} 
	};
	gpio_setup(qpins,99);
	if (lines <= 2) { // for memories which needs HOLD/WP
		gpio_setup_one(PM_D(6),PM_OUT,0);
		gpio_setup_one(PM_D(7),PM_OUT,0);
		gpio_out(PM_D(6),1);
		gpio_out(PM_D(7),1);
	}
	// start at 21MHz, should be ok for all boards
	qspi_set_divider(3); 
}

int split_dma;
void wait_dma()
{
	while (!(HPDMA1_Channel12->CSR & DMA_CSR_TCF)) coro_yield();
}

static void read_by_dma(void *dst,int bytes)
{
	// 0x50400000
//	call_smc(SMC_CHK_IAC,0,0);
	HPDMA1_Channel12->CTR1 = DMA_CTR1_SDW_LOG2_1|DMA_CTR1_DDW_LOG2_1|DMA_CTR1_DINC;
	HPDMA1_Channel12->CTR2 = 2|DMA_CTR2_TCEM_0; // OSPI1
	HPDMA1_Channel12->CBR1 = (bytes/8)|(7<<16);
	HPDMA1_Channel12->CSAR = (uint32_t)&QUADSPI->DR;
	HPDMA1_Channel12->CDAR = (uint32_t)dst;
	HPDMA1_Channel12->CFCR = 0x7f00;
	HPDMA1_Channel12->CCR = DMA_CCR_EN;

	// invalidate dst cache in meantime
	for (;bytes>0;bytes-=64,dst += 64) __asm__ volatile(
                "dc  ivac,%0"::"r"(dst) );
	if (!split_dma) wait_dma();
//	call_smc(SMC_CHK_IAC,0,0);
}

uint32_t qs_ccr_or;
int qspi_xact(int cmd,int addr,int mode,uint32_t *_buf,int len)
{
//	mode &= ~QDMA;
	uint8_t *buf = (uint8_t*)_buf; // current code is byte-based
	int asz = (mode >> 4) & 7;
	int dwid = (mode & (QMO_DBL|QMO_QDL) ? OCTOSPI_CCR_DMODE_1 : 0)|
		(mode & QMO_DBL ? 0 : OCTOSPI_CCR_DMODE_0);
	QUADSPI->CR = (QUADSPI->CR & ~(OCTOSPI_CR_DMAEN|OCTOSPI_CR_FMODE_Msk) )
		|(mode & QDMA ? OCTOSPI_CR_DMAEN:0)
		|((mode & QMO_RD) << OCTOSPI_CR_FMODE_Pos);
	QUADSPI->DLR = len ? len - 1 : 0;
	QUADSPI->TCR = mode & QDMY ? 8 : 0;
	if (qs_ccr_or) QUADSPI->CCR = qs_ccr_or; 
	else QUADSPI->CCR = (len ? dwid : 0)
			|(((asz - 1) & 3) << OCTOSPI_CCR_ADSIZE_Pos)
			|(asz ? OCTOSPI_CCR_ADMODE_0 : 0)
			|(mode & QNOINS ? 0 : OCTOSPI_CCR_IMODE_0);
	QUADSPI->IR = cmd;
	if (asz) QUADSPI->AR = addr;
	if (mode & QDMA) {
		read_by_dma(buf,len);
		return 0;
	}
	int tmo = 0, olen = len;
	while (len>0 && mode & 3)  {
		int st = QUADSPI->SR;
		int lev = (st >> OCTOSPI_SR_FLEVEL_Pos) & 0x7f;
		if (mode & QMO_WR) lev = 64-lev;
		int sz = len < lev ? len : lev; // smaller one
		if (!lev || (lev<4 && len>=4)) {
			coro_yield();
			if (++tmo > 100000) {
				xprintf("QSPI tmo cmd=%X a=%X m=%X l=%d al=%d st=%X\n",
						cmd,addr,mode,olen,len,st);
				return -1;
			}
			continue; 
		}
		tmo = 0;
		len -= sz;
		// use byte only xfers, old size-adaptive code was too buggy
		if (mode & QMO_RD) {
			for (;sz>0;sz--) *(buf++) = *(uint8_t*)&QUADSPI->DR; 
		} else {
			for (;sz>0;sz--) *(uint8_t*)&QUADSPI->DR = *(buf++); 
		}
	}
	return 0;
}

void qspi_wait()
{
	while (QUADSPI->SR & OCTOSPI_SR_BUSY) coro_yield();
}

static uint32_t nor_read_id()
{
	uint32_t buf = 0;
	qspi_xact(0x90,0,QMO_RD|QAD(3),&buf,2);
	return buf;
}

static uint32_t nand_read_id()
{
	// we know at least 3 fmt:
	// Winbond NAND : 9f dummy mfg idh idl
	// Macron NOR   : 9f mfg idh idl
	// Alliance NAND: 9f addr_in mfg id
	// we use 9f and 4B read always and then mask them out 
	uint32_t buf = 0;
	qspi_xact(0x9f,0,QMO_RD,&buf,4);
	return buf;
}

static uint32_t flash_status(struct mtd_dev_t *dev)
{
	uint32_t buf = 0;
	if (MTD_ISNAND(dev->chip)) 
		qspi_xact(0xf,0xc0,QMO_RD|QAD(1),&buf,1);
	else
		qspi_xact(0x5,0,QMO_RD,&buf,1);
	return buf;
}

static uint32_t flash_wait(struct mtd_dev_t *dev)
{
	qspi_wait();
	uint32_t sts;
	while ((sts=flash_status(dev)) & 1) coro_yield();
	//xprintf("fstat %X\n",sts);
	return sts;
}

static void nand_unlock()
{
	qspi_xact(0x1f,0xa000,QAD(2),NULL,0);
	qspi_wait();
}

static int read_pg_common(struct mtd_dev_t *dev,uint8_t *buf,int sz,
		uint32_t addr,int asz)
{
	int flg = QAD(asz)|QDMY|QMO_RD;
	// employ DMA for long and aligned xfers
	if (sz>=256 && !(sz & (DMA_BLKSZ-1))) flg |= QDMA;

	switch (dev->drv_priv[1]) {
		case 1: qspi_xact(0xb,addr,flg,(uint32_t*)buf,sz); break;
		case 2: qspi_xact(0x3b,addr,flg|QMO_DBL,(uint32_t*)buf,sz);
			break;
		case 4: qspi_xact(0x6b,addr,flg|QMO_QDL,(uint32_t*)buf,sz);
			break;
	default: return -1;
	}
	return 0;
}

static int nand_column(struct mtd_dev_t *dev,uint32_t page)
{
	const struct mtd_chip_t *chip = dev->chip;
	if (!(chip->flags & MTDF_2PL)) return 0;
	// for dual page dup EB LSB to column MSB+1 to select
	// correct part of cache register
	return ((page >> chip->eb_sh) & 1) << (chip->page_sh+1);
}

#define CHK_BUF if (!sz || ((uint32_t)buf & 3)) return -1
// returns 0 for ok, -1 for error, >0 for ecc warning
static int nand_read_page(struct mtd_dev_t *dev,uint32_t page,uint8_t *buf,int sz)
{
	CHK_BUF;
	qspi_xact(0x13,page,QAD(3),NULL,0);
	int ecc = (flash_wait(dev) >> 4) & 7;
	if (ecc) dev->ecc_errs++;
	if (read_pg_common(dev,buf,sz,nand_column(dev,page),2)) return -1;
	if (ecc == 2) return -2;
	return ecc;
}

static uint32_t nor_addr(struct mtd_dev_t *dev,uint32_t page)
{
	return page << dev->chip->page_sh;
}

static int flash_erase(struct mtd_dev_t *dev,uint32_t page)
{
	if (MTD_ISNAND(dev->chip)) nand_unlock();
	else page = nor_addr(dev,page);
	qspi_xact(0x6,0,0,NULL,0);
	qspi_wait();
	qspi_xact(0xd8,page,QAD(3),NULL,0);
	int sts = flash_wait(dev);
	//xprintf("erase %d sts %X\n",page,sts);
	int msk = MTD_ISNAND(dev->chip) ? 4 : 0x20;
	return sts & msk ? -2 : 0;
}

static int nand_write_page(struct mtd_dev_t *dev,uint32_t pg,const uint8_t *buf,int sz)
{
	CHK_BUF;
	qspi_xact(0x6,0,0,NULL,0);
	qspi_wait();
	qspi_xact(0x2,nand_column(dev,pg),QMO_WR|QAD(2),(uint32_t*)buf,sz);
	qspi_wait();
	qspi_xact(0x10,pg,QAD(3),NULL,0);
	int sts = flash_wait(dev);
	return sts & 8 ? -2 : 0;
}

static int nor_read_page(struct mtd_dev_t *dev,uint32_t page,uint8_t *buf,int sz)
{
	CHK_BUF;
	if (read_pg_common(dev,buf,sz,nor_addr(dev,page),3)) return -1;
	return 0;
}

static int nor_write_page(struct mtd_dev_t *dev,uint32_t pg,const uint8_t *buf,int sz)
{
	const struct mtd_chip_t *chip = dev->chip;
	CHK_BUF;
	qspi_xact(0x6,0,0,NULL,0);
	qspi_wait();
	int addr = nor_addr(dev,pg);
	//xprintf(" nor wr to %X\n",addr);
	qspi_xact(0x2,addr,QMO_WR|QAD(3),(uint32_t*)buf,sz);
	int sts = flash_wait(dev);
	if (chip->flags & MTDF_NWST) return 0;
	return sts & 0x40 ? -2 : 0;
}

int mtd_nand_get_feat(unsigned addr)
{
	uint32_t buf;
	qspi_xact(0xf,addr & 0xff,QMO_RD|QAD(1),&buf,1);
	qspi_wait();
	return buf & 0xff;
}

int mtd_nand_set_feat(unsigned addr,unsigned val)
{
	addr = (addr<<8) & 0xff00;
	val &= 0xff;
	qspi_xact(0x1f,addr|val,QAD(2),NULL,0);
	qspi_wait();
	return 0;
}

#if 0
static void nand_wait()
{
	uint32_t buf=1;
	qspi_wait();
	while (buf & 1) {
		qspi_xact(0xf,0xc0,QMO_RD|QAD(1),&buf,1);
	}
}
#endif
int mtd_generic_read_seq(struct mtd_dev_t *dev,uint32_t page,uint8_t *buf,
		int pg_cnt,int flags);
int mtd_detect_qspi(struct mtd_dev_t *dev,int bus,int flags)
{
	int data_lines = 2;
	qspi_init(data_lines);
	qspi_wait();
	uint32_t tmp;
//ag:
	qspi_xact(0xff,0,QMO_WR,&tmp,0); // send reset
	qspi_wait();
	int id = nor_read_id(), nand = 0;
	if (!id || (id & 0xffff) == 0xffff) {
		nand = 1;
		id = nand_read_id();
	} else {
		qspi_xact(0xf0,0,QMO_WR,&tmp,1); // nor soft reset
		qspi_wait();
	}
	//xprintf("mtd read id %x\n",id);
	const struct mtd_chip_t *chip = mtd_lookup_flash(id);
	if (!chip) {
		xprintf("unknown qspi chip %X/%d\n",id,nand);
//		udelay(1000000); goto ag;
		return -1;
	}
	memset(dev,0,sizeof(*dev));
	dev->chip = chip;
	dev->drv_name = nand ? "QSPI-NAND" : "QSPI-NOR";
	dev->drv_priv[0] = bus;
	dev->drv_priv[1] = data_lines; // paralel bits for QSPI reading
	dev->erase = flash_erase;
	if (chip->init_fn) chip->init_fn(dev);
	if (nand) {
		dev->read_page = nand_read_page;
		dev->write_page = nand_write_page;
	} else {
		dev->read_page = nor_read_page;
		dev->write_page = nor_write_page;
	}
	dev->read_seq = mtd_generic_read_seq;
	return 0;
}

