NLIB?=/usr/lib/picolibc/aarch64-linux-gnu/lib/
MKIMAGE?=/home/devik/bin/mkimage

MPU_TYPE=25

DEF=-D__AARCH64__ -DSTM32MP257Cxx -DSTM32MP25xxxx -DCORE_CA35 -DCORTEX_IN_SECURE_STATE -DUSE_STM32MP257F_EV1 -DUSE_DEVELOPMENT_MODE -DUSE_BSP_V2 -DUSE_HAL_DRIVERx -DUSE_FULL_LL_DRIVER -D__LOG_UART_IO_ -DLOGLEVEL=LOGERROR -DDDR_SIZE_Gb=2 -DDDR_FREQ=400 -DSTM32MP_DDR_DUAL_AXI_PORT=1 -DDDR_INTERACTIVEx -DSTM32MP_DDR3_TYPE=1 -DSTM32MP_DDR4_TYPE=0 -DSTM32MP_LPDDR4_TYPE=0 -D__unused="__attribute__((unused))" -DSTM32MP_DDR_16_BIT_INTERFACE=1 -DLZO_CFG_NO_UNALIGNED=1
#DEF+=-DGIT='"$(shell git rev-parse --short HEAD)"'
DDR_DIR=ddr_mp2
DDR_SRC=$(wildcard $(DDR_DIR)/*.c)

DEF+=-DLZO_CFG_NO_UNALIGNED=1
CROSS_COMPILE?=aarch64-linux-gnu-
INCS=-ICMSIS/Core_A/Include -I. -ISTM32DDRFW-UTIL/DDR_Tool/lump2A/Inc -Iddr_mp2
CFLAGS=-Os -Wall -Wno-pointer-sign -Wno-main -Wno-unused-function -gdwarf-4 -falign-functions=4 -fno-common -std=gnu99 $(INCS) -DGIT='"$(shell git rev-parse --short HEAD)"' -DMPU_TYPE=$(MPU_TYPE) -Wno-pointer-to-int-cast $(DEF) -mcmodel=tiny -fno-pic -fno-pie -fno-plt
CC=$(CROSS_COMPILE)gcc -mcpu=cortex-a35 -D_GNU_SOURCE=1 -ffreestanding -mstrict-align
LIBGCC:=$(shell $(CC) -print-libgcc-file-name)
LDFLAGS=-static -g -L$(dir $(LIBGCC)) -L$(NLIB) -lc -lgcc
OCOPY=$(CROSS_COMPILE)objcopy
LD=$(CROSS_COMPILE)ld

#SRC=qspi_mp1.c uimage.c fdt.c xprintf.c psci.c coro.c console_stm32mp1.c mtd.c mtd_prog.c stm32mp1.c ca7.c smi.c modules.c linux.c stm32mp1_ddr.c
SRC=qspi_mp1.c uimage.c fdt.c xprintf.c coro.c console_stm32mp1.c mtd.c mtd_prog.c stm32mp1.c ca7.c smi.c modules.c minilzo.c linux.c $(DDR_SRC)

#-include mach/Makefile

.SECONDARY:

all: boot_mp2.b64

.PHONY: always

%.hex: %.elf
	$(OCOPY) -j .vectors -j .text -j .rodata -j .mdata -O ihex $? $@

%.bin: %.elf
	$(OCOPY) -O binary $? $@

%.stm32: %.bin
	$(MKIMAGE) -T stm32image -a 0x2ffc2500 -d $? $@

# MP13 image
%.b32: %.bin
	stm32image -s $^ -d $@ -b 0 -e 0x2ffe0000 -l 0

# MP25 image
#	stm32image -s $^ -d $@ -b 0x30 -e 0x0e003000 -l 0x0e002600 -n 2
%.b64: %.bin
	stm32image -s $^ -d $@ -b 0 -e 0x0e003000 -l 0 -n 2

_sboot.o: start.o secure.o scmi.o psci.o etm.o
	$(LD) -o $@ -r $^ $(LDFLAGS) -Map=maps.txt
	size $@

_sboot2.o: _sboot.o Makefile
	$(OCOPY) $< $@ -G _ResetA35

boot_mp2.elf: mp$(MPU_TYPE).ld _sboot2.o el1.o boot_mp2.o $(patsubst %.c,%.o,$(SRC)) 
	$(LD) -o $@ -T $^ $(LDFLAGS) -Map=map.txt
	size $@

deps:
	$(CC) $(INCS) $(DEFS) -MM $(SRC) $(MACH) > Makefile.deps
#	$(CC) $(INCS) -MM $(COREFILES) |sed -E -e 's/^(\S+): (lwip\S+)\.c/\2.o: \2.c/' >> Makefile.deps

%.o: %.s
	$(CC) -c -o $@ $(CFLAGS) $<

%.o: %.c  
	$(CC) -c -o $@ $(CFLAGS) $<

%.s: %.c  
	$(CC) -S -g -o $@ $(CFLAGS) $<

clean:  
	rm -f *.o *.stm32 *.b32 *.b64 *.elf *.bin ddr_mp2/*.o

-include Makefile.deps
