#include <string.h>
#include "stm32mp1.h"
#include "system.h"
#include "mtd.h"
#include "coro.h"
#include "xprintf.h"
#include "gpio.h"
#include "stm32mp157axx_cm4.h"

#define QMO_RD  1
#define QMO_WR  2
#define QMO_DBL  4
#define QMO_QDL  8
#define QAD(n)  (n<<4) // address bytes 0..4
#define QNOINS  0x80
#define QDMY  0x100
#define QDMA  0x200

void qspi_set_divider(int div)
{
	xprintf("QSPI: set divider %d\n",div);
	QUADSPI->CR = (QUADSPI->CR & ~QUADSPI_CR_PRESCALER_Msk)
		|((div-1) << QUADSPI_CR_PRESCALER_Pos);
}

#define DMA_BLKSZ 16
static void qspi_init()
{
	RCC->QSPICKSELR = 0; // use AXI clk
	RCC->MP_AHB6ENSETR = RCC_MC_AHB6ENCLRR_QSPIEN|RCC_MC_AHB6ENSETR_MDMAEN;
	(void)RCC->MP_AHB6ENSETR;

	QUADSPI->CR = 0;
	udelay(100);
	QUADSPI->DCR = 27 << QUADSPI_DCR_FSIZE_Pos; // 256MB size
	QUADSPI->CR = QUADSPI_CR_EN|((DMA_BLKSZ-1)<<QUADSPI_CR_FTHRES_Pos);

	struct gpio_setup_t qpins[] = {
		{ PM_B(6), PM_ALT|PM_SPD(2), 10, },
		{ PM_F(8), PM_ALT|PM_SPD(2), 10, },
		{ PM_F(9), PM_ALT|PM_SPD(2), 10, },
		{ PM_F(10), PM_ALT|PM_SPD(2), 9, }
	};
	gpio_setup(qpins,sizeof(qpins)/sizeof(struct gpio_setup_t));

	// start at 21MHz, should be ok for all boards
	qspi_set_divider(3); 
}

static void qspi_set_port(int p)
{
	QUADSPI->CR = (QUADSPI->CR & ~QUADSPI_CR_FSEL)|(p?QUADSPI_CR_FSEL:0);
}

static void read_by_mdma(void *dst,int bytes)
{
	// use channel 0 for QSPI read
	MDMA_Channel0->CIFCR = 0x1f;	// clear all flags
	MDMA_Channel0->CTBR = 22;	// QSPI FIFO trigger
	MDMA_Channel0->CSAR = (int)&QUADSPI->DR;
	MDMA_Channel0->CDAR = (int)dst;
	MDMA_Channel0->CBNDTR = bytes;
	MDMA_Channel0->CTCR = MDMA_CTCR_BWM|((DMA_BLKSZ-1)<<MDMA_CTCR_TLEN_Pos)|
		MDMA_CTCR_DINCOS_1|MDMA_CTCR_SINCOS_1|
		MDMA_CTCR_DBURST_0|MDMA_CTCR_SBURST_0|
		MDMA_CTCR_DSIZE_1|MDMA_CTCR_SSIZE_1|MDMA_CTCR_DINC_1;
	MDMA_Channel0->CCR = MDMA_CCR_EN;

	while (!(MDMA_Channel0->CISR & MDMA_CISR_CTCIF)) coro_yield();
}

int qspi_xact(int cmd,int addr,int mode,uint32_t *buf,int len)
{
	int asz = (mode >> 4) & 7;
	int dwid = (mode & (QMO_DBL|QMO_QDL) ? QUADSPI_CCR_DMODE_1 : 0)|
		(mode & QMO_DBL ? 0 : QUADSPI_CCR_DMODE_0);
	QUADSPI->CR = (QUADSPI->CR & ~QUADSPI_CR_DMAEN)
		|(mode & QDMA ? QUADSPI_CR_DMAEN:0);
	QUADSPI->DLR = len ? len - 1 : 0;
	QUADSPI->CCR = ((mode & QMO_RD) << QUADSPI_CCR_FMODE_Pos)
			//|(1<<29) // FRCM
			|(mode & QDMY ? 8<<QUADSPI_CCR_DCYC_Pos : 0)
			|(len ? dwid : 0)
			|(((asz - 1) & 3) << QUADSPI_CCR_ADSIZE_Pos)
			|(asz ? QUADSPI_CCR_ADMODE_0 : 0)
			|(mode & QNOINS ? 0 : QUADSPI_CCR_IMODE_0)
			|cmd;
	if (asz) QUADSPI->AR = addr;
	if (mode & QDMA) {
		read_by_mdma(buf,len);
		return 0;
	}
	int tmo = 0, olen = len;
	while (len>0 && mode & 3)  {
		int st = QUADSPI->SR;
		int lev = (st >> QUADSPI_SR_FLEVEL_Pos) & 0x3f;
		if (mode & QMO_WR) lev = 32-lev;
		int sz = len < lev ? len : lev; // smaller one
		if (!lev || (lev<4 && len>=4)) {
			coro_yield();
			if (++tmo > 1000000) {
				xprintf("QSPI tmo cmd=%X a=%X m=%X l=%d al=%d st=%X\n",
						cmd,addr,mode,olen,len,st);
				//asm("bkpt");
				return -1;
			}
			continue; 
		}
		// xfer size, len for 1,2,3, multiple of 4 for larger
		sz = sz < 4 ? sz : (sz & 0x3c);
		tmo = 0;
		len -= sz;
		if (mode & QMO_RD) {
			for (;sz >= 4;sz -= 4) *(buf++) = QUADSPI->DR; 
			if (sz & 3) {
				uint16_t *p = (void*)buf;
				if (sz >= 2) *(p++) = *(uint16_t*)&QUADSPI->DR;
				if (sz & 1) *p = *(uint8_t*)&QUADSPI->DR;
			}
		} else {
			for (;sz >= 4;sz -= 4) QUADSPI->DR = *(buf++);
			if (sz & 3) {
				uint16_t *p = (void*)buf;
				if (sz >= 2) *(uint16_t*)&QUADSPI->DR = *(p++);
				if (sz & 1) *(uint8_t*)&QUADSPI->DR = *p;
			}
		}
	}
	return 0;
}

void qspi_wait()
{
	while (QUADSPI->SR & QUADSPI_SR_BUSY) coro_yield();
}

static uint32_t nor_read_id()
{
	uint32_t buf = 0;
	qspi_xact(0x90,0,QMO_RD|QAD(3),&buf,2);
	return buf;
}

static uint32_t nand_read_id()
{
	uint32_t buf = 0;
	qspi_xact(0x9f,0,QMO_RD|QAD(1),&buf,2);
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

static uint32_t nand_unlock()
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
	// for dual page dup EB LSB to column MSB+1
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
	CHK_BUF;
	qspi_xact(0x6,0,0,NULL,0);
	qspi_wait();
	int addr = nor_addr(dev,pg);
	//xprintf(" nor wr to %X\n",addr);
	qspi_xact(0x2,addr,QMO_WR|QAD(3),(uint32_t*)buf,sz);
	int sts = flash_wait(dev);
	return sts & 0x40 ? -2 : 0;
}

static uint32_t nand_continuous_off()
{
	uint32_t buf;
	qspi_xact(0xf,0xb0,QMO_RD|QAD(1),&buf,1);
	qspi_wait();
	buf &= 0xff;
	if ((buf & 8)==0) {
		xprintf("NAND, turn cont. mode off (st=%X)\n",buf);
		qspi_xact(0x1f,0xb000|buf|8,QAD(2),NULL,0);
	}
	qspi_wait();
}

static void nand_wait()
{
	uint32_t buf=1;
	qspi_wait();
	while (buf & 1) {
		qspi_xact(0xf,0xc0,QMO_RD|QAD(1),&buf,1);
	}
}

int mtd_generic_read_seq(struct mtd_dev_t *dev,uint32_t page,uint8_t *buf,
		int pg_cnt,int flags);
int mtd_detect_qspi(struct mtd_dev_t *dev,int bus,int flags)
{
	qspi_init();
	qspi_set_port(bus);
	qspi_wait();
	uint32_t tmp;
	qspi_xact(0xff,0,QMO_WR,&tmp,1); // send reset
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
		return -1;
	}
	memset(dev,0,sizeof(*dev));
	dev->chip = chip;
	dev->drv_name = nand ? "QSPI-NAND" : "QSPI-NOR";
	dev->drv_priv[0] = bus;
	dev->drv_priv[1] = 2; // paralel bits for QSPI reading
	dev->erase = flash_erase;
	if (nand) {
		dev->read_page = nand_read_page;
		dev->write_page = nand_write_page;
		if (chip->flags & MTDF_CONT) nand_continuous_off();
	} else {
		dev->read_page = nor_read_page;
		dev->write_page = nor_write_page;
	}
	dev->read_seq = mtd_generic_read_seq;
	return 0;
}

