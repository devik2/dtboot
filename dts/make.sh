#!/bin/bash

make -C /home/devik/proj/linux/st_lin_main ARCH=arm CROSS_COMPILE=arm-none-eabi- M=$PWD
for i in lump1-*.dtb; do
	N=${i%.dtb}
	mkimage -A arm -n $N -a 0xc1f00000 -d $i /tmp/$N.dtu
done
