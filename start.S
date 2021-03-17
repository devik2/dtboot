
	.arm
_start:
	b reset
	b loop
	b smc
	b loop
	b loop
	b loop
	b irq
	bkpt
	.extern prog_ext,aux_entry, irq_handler
	.weak irq_handler
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
	@ 0x48, git revision pointer
	.word git_version
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

	bl	inv_mem
	bl	def_sctlr
	bl	set_sec			@ switch to secure SVC
	mov	r0, r10
	bl 	main
loop:	b loop

	.globl setup_ns_mode
setup_ns_mode:
	ldr	r1, =_start
	mcr	p15, 0, r1, c12, c0, 0	@ non-secure VBAR
	mov	pc,lr
set_sec:				@ set security, sp is this CPU mstk
	mov	r1, #0x3fff
	mcr	p15, 0, r1, c1, c1, 2	@ enable all CP access in NSACR
	ldr	r1, =_start		@ assume we are in secure mode 
	mcr	p15, 0, r1, c12, c0, 1	@ write MVBAR
	mcr	p15, 0, r1, c12, c0, 0	@ secure VBAR
	mov	r0, sp
	mov	r3, lr
	set_mode #0x13			@ SVC mode (secure)
	sub	sp, r0, #0x400		@ SVC stack
	mov	pc,r3
inv_mem:
	mov	r0, #0			@ set up for MCR
	mcr	p15, 0, r0, c8, c7, 0	@ invalidate TLBs
	mcr	p15, 0, r0, c7, c5, 0	@ invalidate icache
	mcr	p15, 0, r0, c7, c5, 6	@ invalidate BP array
	mcr     p15, 0, r0, c7, c10, 4	@ DSB
	mcr     p15, 0, r0, c7, c5, 4	@ ISB
	nop
	mov	pc,lr
def_sctlr:
	mrc	p15, 0, r0, c1, c0, 0	@ SCTLR
	bic	r0, r0, #0x00002000	@ clear bits 13 (--V-)
	bic	r0, r0, #0x00000007	@ clear bits 2:0 (-CAM)
	orr	r0, r0, #0x00000002	@ set bit 1 (--A-) Align
	orr	r0, r0, #0x00001800	@ set bits (IZ---) BTB,Icache
	mcr	p15, 0, r0, c1, c0, 0
	mov	pc,lr

irq:
	push	{r0-r3,r12,lr}
	bl	irq_handler
	pop	{r0-r3,r12,lr}
	subs    pc,lr,#4
smc:	
	push	{r4,lr}
	ldr	r4, =0x83000103		@ switch to NS action
	cmp	r0, r4
	beq	smc_switch
        mrc     p15, 0, r4, c1, c1, 0
        bic     lr, r4, #1
        mcr     p15, 0, lr, c1, c1, 0	@ Switch to secure
        isb
	dsb
	str	r4, p_scr
	bl	smc_handler
	mcr     p15, 0, r4, c1, c1, 0	@ Switch back to orig mode
smc_fin:pop	{r4,lr}
	movs	pc,lr
smc_switch:
	mov	r4, #0x31	@ switch to NS mode
	mcr     p15, 0, r4, c1, c1, 0
	mov	r0, #0			@ zero virt timer offset,
 	mcrr	p15, 4, r0, r0, c14	@ can be set only when NS=1 
	b	smc_fin	

call_smc:
	smc #0
	mov	pc,lr

	.globl get_SCR,get_CPSR,get_SP,call_smc,get_SCTLR,get_ACTLR,get_SPSR
	.globl get_VBAR,get_MVBAR
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
get_VBAR:
	mrc	p15, 0, r0, c12, c0, 0
	mov	pc, lr
get_MVBAR:
	mrc	p15, 0, r0, c12, c0, 1
	mov	pc, lr
	.globl get_arm_time,get_arm_tfreq
get_arm_time:
	mrrc 	p15,0,r0,r1,c14
	mov	pc, lr
get_arm_tfreq:
	mrc	p15,0,r0,c14,c0,0
	mov	pc, lr

	.globl boot_secondary
	.extern cpu2_entry
boot_secondary:
	set_mode #0x16			@ MON
	orr	r2, r2, #0xc0		@ disable FIQ and IRQ
	msr	cpsr,r2
	ldr	sp, mstk2		@ top of stacks for MON

	bl	inv_mem
	bl	def_sctlr
	bl	set_sec			@ switch to secure SVC

	ldr	r0, =cpu2_entry
	bx	r0

	@ coroutine support
	.globl coro_switch
coro_switch: @ switch from r0 to r1
	push {r0,r1,r4-r11,lr}	@ r0,r1 are for init args
	str sp, [r0]
	ldr sp, [r1]
	pop {r0,r1,r4-r11,lr}
	bx lr
	mov pc, lr
