# DTBoot

KISS but complete ARM Linux DT-based bootloader

Note: it is in progress of being cleaned and uploaded..

## Introduction

DTBoot aims to be simple and small ARM (yet) bootloader which
fits into SoC's SRAM. Thus it is single-stage loader.

It started as test code when starting with STM32MP1 and QSPI NAND
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

### Usage

You can customize compiled bootloader by directly editing its
address 0x40 and change for example initial console for different
board.

Often I use openocd to change console or force bootloader to
halt at start when I debug already functional module on new
PCB with different UART usage.
I can also use it when MP1 module is taken from device into
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
  |28|PH13|4 |PZ2|1 |PZ7|1|
  E.g. byte `0x12` sets console to **USART1** at **PA9**.

- `BFI_NONS(3)` Linux kernel is entered in ARM's non-secure mode. Set
  this flag to nonzero to keep CPU in secure mode. Intended for debug mainly.

- `BFI_DTB_ID(4)` Select another DTB. Currently any nonzero value select
  alternative location in MTD device (see `FTD_POS2` in [boot_mp1.c](boot_mp1.c).
  It is planned to translate number to volume name once UBI is implemented.

- `BFI_SHELL(5)` Value is conveyed to kernel in device tree node 
  `dt-boot/enter-shell`. Typicaly initrd based emergency code enters
  shell when set.
