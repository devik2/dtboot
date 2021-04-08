NLIB?=/home/devik/proj/linux/newlib-A9/arm-none-eabi/newlib
MKIMAGE?=/home/devik/bin/mkimage

CROSS_COMPILE?=arm-none-eabi-
INCS=-IInclude -I.
CFLAGS=-Os -fstack-usage -Wall -Wno-pointer-sign -Wno-main -Wno-unused-function -gdwarf-4 -falign-functions=4 -fno-common -std=gnu99 $(INCS) -mno-unaligned-access -DGIT='"$(shell git rev-parse --short HEAD)"'
CC=$(CROSS_COMPILE)gcc -mcpu=cortex-a7 -D_GNU_SOURCE=1
LIBGCC:=$(shell $(CC) -print-libgcc-file-name)
LDFLAGS=-Map=map.txt -static -g -L$(dir $(LIBGCC)) -L$(NLIB) -lc -lgcc
OCOPY=$(CROSS_COMPILE)objcopy
LD=$(CROSS_COMPILE)ld --no-enum-size-warning
SRC=qspi_mp1.c uimage.c fdt.c xprintf.c psci.c coro.c console_stm32mp1.c mtd.c mtd_prog.c stm32mp1.c stm32mp1_ddr.c ca7.c smi.c modules.c linux.c stm32mp1_dsi.c

-include mach/Makefile

.SECONDARY:

all: boot_mp1.stm32

.PHONY: always

%.hex: %.out
	$(OCOPY) -j .vectors -j .text -j .rodata -j .mdata -O ihex $? $@

%.bin: %.out
	$(OCOPY) -j .vectors -j .text -j .rodata -j .mdata -j .data -O binary $? $@

%.stm32: %.bin
	$(MKIMAGE) -T stm32image -a 0x2ffc2500 -d $? $@

boot_mp1.out: ram.lds start.o boot_mp1.o $(patsubst %.c,%.o,$(SRC)) 
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
