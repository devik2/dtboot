#include "smi.h"
#include "system.h"

// -1 means no change, io==2/3 means input/output mode
static int eth_smi_set(const struct smi_iodef *e,int clk,int io)
{
	int rv = gpio_in(e->io_mdio);
	if (clk>=0) gpio_out(e->io_mdc,clk);
	switch (io) {
		case 0:
		case 1: gpio_out(e->io_mdio,io); break;
		case 2: gpio_setup_one(e->io_mdio,PM_IN|PM_PU,0); break;
		case 3: gpio_setup_one(e->io_mdio,PM_OUT,0);
	}
	udelay(2);
	return rv;
}

uint32_t eth_smi_xact(const struct smi_iodef *e,uint32_t x,int rd)
{
	int i; uint32_t r = 0;
	eth_smi_set(e,0,3); // IO out
	for (i=0;i<32;i++,x<<=1) {
		if (rd && i==14) { eth_smi_set(e,-1,2); x = -1; }
		eth_smi_set(e,0,(x>>31)&1);
		r = (r<<1)|eth_smi_set(e,1,-1);
	}
	eth_smi_set(e,0,2);
	return r;
}

int eth_smi_rd(const struct smi_iodef *e,int id,int reg)
{
	eth_smi_xact(e,0xffffffff,0);
	uint32_t r = eth_smi_xact(e,0x60000000|(id<<23)|(reg<<18),1);
	r &= 0xffff;
//	jprintf("SMI %x: %X",reg,r);
	return r;
}

void eth_smi_wr(const struct smi_iodef *e,int id,int reg,uint16_t v)
{
	eth_smi_xact(e,0xffffffff,0);
	eth_smi_xact(e,0x50020000|(id<<23)|(reg<<18)|v,0);
}
