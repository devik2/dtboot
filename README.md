# DTBoot

KISS but complete ARM Linux DT-based bootloader

**Note: it is in progress of being cleaned and uploaded..**

## Introduction

DTBoot aims to be simple and small ARM (yet) bootloader which
fits into SoC's SRAM. Thus it is single-stage loader.
It is configured by the same DTB as used by kernel later.

It originated as test code when begining with STM32MP1 and QSPI NAND
boot was not supported in uBoot nor TF-A at that time.
We needed to customize startup code without looking for expert
fluent with uBoot codebase.

DTBoot is mode akin to LILO as oposed to Grub when compared
with uBoot. We feel that we don't need hundreds kilobytes
of code to just boot Linux.

Current developement (but working) code includes MTD subsystem 
with QSPI and both NAND / NOR support, coroutines, serial console
(output only), MP1 DDR startup, PSCI, FTD/DTB parser, uImage
support and embedded JTAG based flash uploader:

    -rw-r--r-- 1 devik devik   38156 Nov 21 18:25 boot_mp1.stm32

It is not slow too. Even with DSI start in bootloader and showing
gzipped splash image, loader tooks about 200ms (when using coroutines).

## Our prototypical usage

The content of flash should be simple and easy to understand not
only for Linux expert but for average MCU programmer - because
both STM32MP1 and our MP1 SOM targets them.

Typically for 128MB QSPI NAND we simply have first erase block (128k)
reserved for DTBoot, at 512k offset there is DTB, at 1MB offset
we have Linux kernel (with appended rescue initrd which also creates
RW overlays) and
from 5MB there is big UBI space (for squashfs with system and RW data).

This allows for simple in-system updates.

First bringup of new SOM is done on tester. There DTBoot is uploaded
to run some system tests. If ok, kernel is programmed via JTAG and DTBoot
into the flash. The kernel is run and it automaticaly ends up in the rescue
initrd (because it can't find valid UBI on the flash).
Now (as we are in Linux already) we can attach network over USB
in order to format and populate UBI with main filesystem.

## TODO

- coroutines was disabled during code cleanup, enable them
- Zynq version
- UBI support
- authenticated boot

## Boot flow on STM32MP1

DTBoot is loaded by SoC ROM code (and optionaly authenticated in
case SoC implements authenticated boot) and starts in `start.S`
file (basic Cortex A initialization).

Next clock for all relevant periphs are enabled (still running on 
64MHz HSI), successfull boot acked to power MCU on our MP1 module
(works around STM32MP1 errata with stuck clock muxes), and boot flags
are read (see section **Boot flags** below).

Then we setup STGEN with HSI (to allow for basic timings) and
start serial console early (also on HSI).
Both of these will be later reverted to other (more precise)
clock once main configuration is known.

Now QSPI attached MTD chip is detected (TODO: MTD interface 
selection will be governed by boot flags).

Now, there is provision for JTAG debugger (e.g. openocd) to 
use DTBoot code to perform SoC debug/MTD programming.
When DTBoot is loaded by debugger and its 0x2c relative address
is set to nonzero (1 for flash programmer), selected custom code
is run instead of regular boot.

Else appropriate MTD address (according to bootflags) is loaded as
uImage which contains DTB. 
DTB is then used by both DTBoot and kernel.

Remainder of boot is controlled by running modules described
in `/dt-boot/*` sections.

## Boot flags

Boot flags can alter behavior of DTBoot before main DTB
file is loaded.
Especially we need to set/modify serial console settings,
early debug breakpoints and MTD parameters (to be able to
load initial DTB).

The boot flags can be sourced from two places for STM32MP1 platform.
The first one is at fixed offset (0x40..0x47) in the bootloader
binary.
The second one is TEMP storage (part of RTC subdomain). The TEMP
one uses regs 18 and 19 (also see 
[ST TEMP usage](https://wiki.st.com/stm32mpu/wiki/STM32MP15_backup_registers).
TEMP flags have higher priority.

Currently there is 15 possible flags each with value 0..15 (or
missing, which is encoded as -1/0xff). Hence we can express
each flag as one byte (e.g. 0x2c sets flag 2 to value 0xc).
To make it more secure, flag setting bytes must finish by
flag 0 (CHECK) whose value is XOR of all prev nibbles.
So that byte 0x00 alone is correct boot flags sequence (empty one).

For STM32MP1, constants are defined in [stm32mp1.h](stm32mp1.h) as
`BFI_XXX`.

### Flags usage

You can customize compiled bootloader by directly editing its
address 0x40 (where flags reside) and change for example initial 
console for different boards.

Often I use openocd to change console or force bootloader to
halt at start when I debug already functional module on new
PCB with different UART wiring.
I can also use it when MP1 module is taken from device back into
tester - then I add DTB override bootflag to boot linux with
alternative DTB tailored for use in the tester.

### STM32MP1 defined flags

- `BFI_UARTL(1)` set console to given UART 0..15
- `BFI_UARTH(2)` set console to given UART 16..31
  |No|Pin+0|USART|Pin+1|USART|Pin+2|USART|Pin+3|USART|
  |---|---|---|---|---|---|---|---|---|
  |0|PA0|4 |PA2|2 |PA9|1 |PA12|4|
  |4|PA13|4 |PA15|7 |PB4|7 |PB6|1|
  |8|PB6|5 |PB9|4 |PB10|3 |PB13|5|
  |12|PB14|1 |PC6|6 |PC8|4 |PC10|3|
  |16|PC10|4 |PC12|5 |PD1|4 |PD5|2|
  |20|PD8|3 |PE1|8 |PE8|7 |PF5|2|
  |24|PF7|7 |PG11|1 |PG11|4 |PG14|6|
  |28|PH13|4 |PZ2|1 |PZ7|1| ||

  E.g. byte `0x12` sets console to **USART1** at **PA9**.

- `BFI_NONS(3)` Linux kernel is entered in ARM's non-secure mode. Set
  this flag's bit 0 to 1 to keep CPU in secure mode. 
  Bit 1 forces bkpt just after mode change.
  Intended for debug mainly.

- `BFI_DTB_ID(4)` Select another DTB. Currently any nonzero value select
  alternative location in MTD device (see `FTD_POS2` in [boot_mp1.c](boot_mp1.c).
  It is planned to translate number to volume name once UBI is implemented.

- `BFI_SHELL(5)` Value is conveyed to kernel in device tree node 
  `dt-boot/enter-shell`. Typicaly initrd based emergency code enters
  shell when set.

## DT configuration

Once DTB is loaded, its `dt-boot` subsection is inspected. All
subsections should be named as `name@nn` where nn are decimal
digits.
All subsections are sorted by `nn` before being executed.

Then each subsection's `compatible` property is examined and
if modules' driver is known, it is executed.

Let's look at simple example:

```dts
\ {
	dt-boot {
		// these are overriden by bootloader
                enter-shell = <0>;
                boot-flags = <0 0 0 0>;
		psci-cpu-on = <1>;

		ddr@60 {
                        compatible = "mp1,ddr3";
                        patch-size-to = <&main_mem>;
                };
	};
	main_mem: memory@c0000000 {
                device_type = "memory";
                reg = <0xc0000000 0x04000000>; // 64MB is minimum here
        };
};
```

`enter-shell` and `boot-flags` are placeholders - DTBoot fills them
and linux userspace can read them in `/sys/firmware/devicetree`.

`psci-cpu-on` enables or disables (default) ability to power secondary
CPU on. 

`ddr@60` block uses `mp1,ddr3` driver contained in DTBoot and instructs
it to patch autodetected DDR size to `reg` of `main_mem`.
Similarly, size of flash can be patched to Linux MTD partition table.

### driver 'cob,ldlinux'

Loads kernel uImage from MTD offset in `linux-img-start` property
and runs it.

If `linux-rd-start` is defined, loads RD too and patches its location
into `/chosen` DTB node.

Load addresses for both kernel and RD are read from uImage header.

### driver 'cob,mtdfix'

Fixes size in partition subnode of **fixed-partitions** as referenced
by `patch-size-to` property. Size is set to flash size minus partition
offset minus 8 erase blocks (leaving space for BBT).

### driver 'mp1,console'

(Re-)configure serial console to another one. `uid` is ID
of existing UART/pin combination from table in `BFI_UARTL` description.
`mp1,hse-khz`, if present, conveys HSE freq and forces its use instead of HSI.
`baud-rate` is serial baud rate in bps. 115200 is used if missing.

`sync-console` is bool property which indicates that console is synchronous
(as oposed to async coroutine based). Can be used to debug timing errors
but renders boot slower.

### driver 'mp1,hse', 'mp1,csicomp', 'mp1,lse', 'mp1,rtc'

Enables given hardware. RTC should be last (it waits for LSE and
it can be 1000ms sometimes).

### driver 'mp1,ddr3'

Starts DDR3, PLL2.R must be set correctly up (min 300MHz). It uses
default generic DDR3 parameters for now. 
It is planned to add timing params here.
`patch-size-to` can be used to patch autodetected size to `memory` node.

### driver 'mp1,clocks'

Set PLLs up. `mp1,hse-khz` prop gives HSE frequency. If 0 or omited,
64MHz HSI is used.
`mp1,pll1`...`mp1,pll4` = `<M N P Q R>` where M is predivider, N is
multiplier, P/Q/R are post-dividers of outputs. See STM32MP1 reference
manual for details.
`mp1,apb-divs` gives dividers for ABP1..5.
`mp1,rtc-div` is divider for RTC.
`mp1,qspi-div` is QSPI divider from PLL2.P (AXI clk).

If `mp1,hse-khz` is given, STGEN is reconfigured to HSE.

### driver 'mp1,gpio'

Allows to setup gpio muxes. `mp1,mux` sets one mux per value. Use
macros in [dt-boot-gpio.h](dts/dt-boot-gpio.h).
`mp1,wr32` contains triples `<A V M>` and sets 32 bit address 
MEM[A]=(MEM[A]&M)|V.

Example usage to enable MCO clock and ETH_CLK early (or linux can't
use MDIO to communicate with the PHY):
```dts
  compatible = "mp1,gpio";
  mp1,mux = <PM_MUX(PM_B(8),PM_OUT|PM_DFLT(0),0)>, // PHY reset
            <PM_MUX(PM_C(12),PM_OUT|PM_ALT,1)>, // MCO2
            <PM_MUX(PM_G(8),PM_OUT|PM_ALT|PM_SPD(1),2)>; // ETH_CLK
  mp1,wr32 = <0x50000218 0x80 0>, // enable ETH_CK
             <0x50000804 0x1004 0>;  // 24MHz to MCO2
```
