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
case SoC implements authenticated boot) and starts in start.S
file (basic Cortex A initialization).

Next clock for all relevant periphs are enabled (still running on 
64MHz HSI), successfull boot acked to power MCU on our MP1 module
(works around errata with stuck clock muxes), and boot flags
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

Else appropriate MTD address is loaded (according to bootflags) as
uImage which contains DTB - DTB is then used by both DTBoot
and kernel.

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
  this flag to nonzero to keep CPU in secure mode. Intended for debug mainly.

- `BFI_DTB_ID(4)` Select another DTB. Currently any nonzero value select
  alternative location in MTD device (see `FTD_POS2` in [boot_mp1.c](boot_mp1.c).
  It is planned to translate number to volume name once UBI is implemented.

- `BFI_SHELL(5)` Value is conveyed to kernel in device tree node 
  `dt-boot/enter-shell`. Typicaly initrd based emergency code enters
  shell when set.
