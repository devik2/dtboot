
	.arm
_start:
	b reset
	b loop
	b smc
	b loop
	b loop
	b loop
	bkpt
	bkpt
	.extern prog_ext,aux_entry
	.extern smc_handler, set_tz_sec, stm32mp_init_gic
	.weak smc_handler, set_tz_sec, stm32mp_init_gic, aux_entry
prog_a: .word 0
prog_s: .word 0xc0000000
prog_l: .word 0	@ 0x28: no of bytes
prog_x: .word 0 @ if nonzero, special prog. is run instead of boot
	.globl prog_a,prog_s,prog_l,prog_x
	@ 0x30 offset is Thumb entry to run
	.thumb
	ldr r0,=run
	bx r0
	.arm
	@ 0x34 program NOR
	b prog_ext
	@ 0x38 ARM mode run
	b run
	@ 0x3c 
	b aux_entry
	@ 0x40 embeded boot flags
ebflags:.byte 0x12	@ UART1 at PA9
	.byte 0x3,0,0,0,0,0,0
mstk2:	.word 0x2ffe0000
mstk:	.word 0x2ffdf000
p_scr:	.word 0			@ SCR on SMC entry stored here
	.globl p_scr,ebflags
run:	mov r10, #0
	b run_cmn
reset:  mov r10, r0		@ boot context
	.extern main

	.macro set_mode m
	mrs	r2, cpsr
	bic	r2, r2, #0x1f	@ mask mode bits
	orr	r2, \m
	msr	cpsr,r2
	.endm

run_cmn:
	mov	r0, #0x50000000		@ RCC
	mov	r1, #0
	str	r1, [r0]		@ turn RCC TZ off

	set_mode #0x16			@ MON
	orr	r2, r2, #0xc0		@ disable FIQ and IRQ
	msr	cpsr,r2
	ldr	sp, mstk		@ top of stacks for MON

	mov	r1, #0x3fff
	mcr	p15, 0, r1, c1, c1, 2	@ enable all CP access in NSACR

	mov	r0, #0			@ set up for MCR
	mcr	p15, 0, r0, c8, c7, 0	@ invalidate TLBs
	mcr	p15, 0, r0, c7, c5, 0	@ invalidate icache
	mcr	p15, 0, r0, c7, c5, 6	@ invalidate BP array
	mcr     p15, 0, r0, c7, c10, 4	@ DSB
	mcr     p15, 0, r0, c7, c5, 4	@ ISB

	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #0x00002000	@ clear bits 13 (--V-)
	bic	r0, r0, #0x00000007	@ clear bits 2:0 (-CAM)
	orr	r0, r0, #0x00000002	@ set bit 1 (--A-) Align
	orr	r0, r0, #0x00000800	@ set bit 11 (Z---) BTB
	orr	r0, r0, #0x00001000	@ set bit 12 (I) I-cache
	mcr	p15, 0, r0, c1, c0, 0

	bl	set_tz_sec
	mov	r4, r0			@ remember if NS switch is needed
	mov	r0, #0
	bl	stm32mp_init_gic

	mov	r1, #0x31
	cmp	r4, #0
	mcrne	p15, 0, r1, c1, c1, 0	@ switch to NS mode after leaving MON

	mov	r0, #0			@ zero virt timer offset, need
 	mcrrne	p15, 4, r0, r0, c14	@ can be set only when NS=1 TODO

	ldr	r1, =_start		@ assume we are in secure mode 
	mcr	p15, 0, r1, c12, c0, 1	@ write MVBAR
	mcr	p15, 0, r1, c12, c0, 0	@ secure VBAR
	set_mode #0x13			@ SVC mode (nonsecure)
	mcr	p15, 0, r1, c12, c0, 0	@ insecure VBAR
	ldr	sp, mstk
	sub	sp, sp, #0x400		@ SVC stack

	mov	r0, r10
	bl main
loop:	b loop

smc:	
	push	{r4,lr}
        mrc     p15, 0, r4, c1, c1, 0
        bic     lr, r4, #1
        mcr     p15, 0, lr, c1, c1, 0	@ Switch to secure
        isb
	dsb
	str	r4, p_scr
	bl	smc_handler
        mcr     p15, 0, r4, c1, c1, 0	@ Switch back to orig mode
	pop	{r4,lr}
	movs	pc,lr

call_smc:
	smc #0
	mov	pc,lr

	.globl get_SCR,get_CPSR,get_SP,call_smc,get_SCTLR,get_ACTLR,get_SPSR
get_SCTLR:
	mrc	p15, 0, r0, c1, c0, 0
	mov	pc, lr
get_ACTLR:
	mrc	p15, 0, r0, c1, c0, 1
	mov	pc, lr
get_SCR:
	mrc	p15, 0, r0, c1, c1, 0
	mov	pc, lr
get_CPSR:
	mrs	r0, cpsr
	mov	pc, lr
get_SPSR:
	mrs	r0, spsr
	mov	pc, lr
get_SP:
	mov	r0, sp
	mov	pc, lr
	.globl get_arm_time,get_arm_tfreq
get_arm_time:
	mrrc 	p15,0,r0,r1,c14
	mov	pc, lr
get_arm_tfreq:
	mrc	p15,0,r0,c14,c0,0
	mov	pc, lr

	.globl boot_secondary, sec_boota
sec_boota:	
	.word 0
boot_secondary:
	set_mode #0x16			@ MON
	orr	r2, r2, #0xc0		@ disable FIQ and IRQ
	msr	cpsr,r2
	ldr	sp, mstk2		@ top of stacks for MON

	mov	r1, #0x3fff
	mcr	p15, 0, r1, c1, c1, 2	@ enable all CP access in NSACR

	mov	r0, #0			@ set up for MCR
	mcr	p15, 0, r0, c8, c7, 0	@ invalidate TLBs
	mcr	p15, 0, r0, c7, c5, 0	@ invalidate icache
	mcr	p15, 0, r0, c7, c5, 6	@ invalidate BP array
	mcr     p15, 0, r0, c7, c10, 4	@ DSB
	mcr     p15, 0, r0, c7, c5, 4	@ ISB

	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #0x00002000	@ clear bits 13 (--V-)
	bic	r0, r0, #0x00000007	@ clear bits 2:0 (-CAM)
	orr	r0, r0, #0x00000002	@ set bit 1 (--A-) Align
	orr	r0, r0, #0x00000800	@ set bit 11 (Z---) BTB
	orr	r0, r0, #0x00001000	@ set bit 12 (I) I-cache
	mcr	p15, 0, r0, c1, c0, 0

	mov	r0, #1
	bl	stm32mp_init_gic

	mov	r1, #0x31
	mcr	p15, 0, r1, c1, c1, 0	@ switch to NS mode after leaving MON

	ldr	r1,=#24000000
	mcr	p15, 0, r1, c14, c0, 0	@ timer freq
	mov	r0, #0			@ zero virt timer offset, need
 	mcrr	p15, 4, r0, r0, c14	@ to be set when NS=1 

	ldr	r1, =_start		@ assume we are in secure mode 
	mcr	p15, 0, r1, c12, c0, 1	@ write MVBAR
	mcr	p15, 0, r1, c12, c0, 0	@ secure VBAR
	set_mode #0x13			@ SVC mode (nonsecure)
	mcr	p15, 0, r1, c12, c0, 0	@ insecure VBAR
	ldr	sp, mstk2
	sub	sp, sp, #0x400		@ SVC stack
@lp11:	b lp11
	ldr	r0, sec_boota
	bx	r0
	mrs	r0, ELR_hyp

	@ coroutine support
	.globl coro_switch
coro_switch: @ switch from r0 to r1
	push {r4-r11,lr}
	str sp, [r0]
	ldr sp, [r1]
	pop {r4-r11,lr}
	bx lr
	mov pc, lr
