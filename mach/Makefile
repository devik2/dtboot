
all: boot_portb.stm32

mach/logo_black_bg.zz: mach/logo_black_bg.png
	python mach/cnv_logo.py $^ $@

%.o: %.zz
	$(OCOPY) -I binary -O elf32-littlearm -B arm --rename-section .data=.rodata $^ $@

boot_portb.out: ram.lds start.o boot_mp1.o $(patsubst %.c,%.o,$(SRC)) \
		mach/portbl_ops.o mach/logo_black_bg.o \
		mach/tinflate.o
	$(LD) -o $@ -T $^ $(LDFLAGS)
	size $@
