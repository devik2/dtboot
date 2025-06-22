NLIB?=/home/devik/proj/linux/newlib-A9/arm-none-eabi/newlib
#NLIB=/usr/lib/arm-none-eabi/newlib/thumb/v7-a/nofp
MKIMAGE?=/home/devik/bin/mkimage

MPU_TYPE=15
MPU_TYPE=13

CROSS_COMPILE?=arm-none-eabi-
INCS=-IInclude -I.
CFLAGS=-Os -fstack-usage -Wall -Wno-pointer-sign -Wno-main -Wno-unused-function -gdwarf-4 -falign-functions=4 -fno-common -std=gnu99 $(INCS) -mno-unaligned-access -DGIT='"$(shell git rev-parse --short HEAD)"' -DMPU_TYPE=$(MPU_TYPE)
CC=$(CROSS_COMPILE)gcc -mcpu=cortex-a7 -D_GNU_SOURCE=1
#LIBGCC:=$(shell $(CC) -print-libgcc-file-name)
LIBGCC= /usr/lib/gcc/arm-none-eabi/12.2.1/arm/v5te/softfp/libgcc.a
LDFLAGS=-Map=map.txt -static -g -L$(dir $(LIBGCC)) -L$(NLIB) -lc -lgcc
OCOPY=$(CROSS_COMPILE)objcopy
LD=$(CROSS_COMPILE)ld
SRC=qspi_mp1.c uimage.c fdt.c xprintf.c psci.c coro.c console_stm32mp1.c mtd.c mtd_prog.c stm32mp1.c ca7.c smi.c modules.c linux.c stm32mp1_ddr.c

#-include mach/Makefile

.SECONDARY:

all: boot_mp1.stm32 boot_mp1.b32

.PHONY: always

%.hex: %.out
	$(OCOPY) -j .vectors -j .text -j .rodata -j .mdata -O ihex $? $@

%.bin: %.out
	$(OCOPY) -j .vectors -j .text -j .rodata -j .mdata -j .data -O binary $? $@

%.stm32: %.bin
	$(MKIMAGE) -T stm32image -a 0x2ffc2500 -d $? $@

# MP13 image
%.b32: %.bin
	stm32image -s $^ -d $@ -b 0 -e 0x2ffe0000 -l 0

boot_mp1.out: mp$(MPU_TYPE)x.lds start.o boot_mp1.o $(patsubst %.c,%.o,$(SRC)) 
	$(LD) -o $@ -T $^ $(LDFLAGS)
	size $@

deps:
	$(CC) $(INCS) $(DEFS) -MM $(SRC) $(MACH) > Makefile.deps
#	$(CC) $(INCS) -MM $(COREFILES) |sed -E -e 's/^(\S+): (lwip\S+)\.c/\2.o: \2.c/' >> Makefile.deps

%.o: %.S  
	$(CC) -c -o $@ $(CFLAGS) $<

%.o: %.c  
	$(CC) -c -o $@ $(CFLAGS) $<

%.s: %.c  
	$(CC) -S -g -o $@ $(CFLAGS) $<

clean:  
	rm -f *.o *.hex *.out

-include Makefile.deps
