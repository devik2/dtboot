#ifndef __SMI_H
#define __SMI_H 1

#include "gpio.h"

struct smi_iodef {
	gport_t io_mdc,io_mdio;
};

int eth_smi_rd(const struct smi_iodef *e,int id,int reg);
void eth_smi_wr(const struct smi_iodef *e,int id,int reg,uint16_t v);

#endif
