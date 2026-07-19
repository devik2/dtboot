
        .balign 0x1000
	.globl _el1_vect
_el1_vect:
1:	b	1b

        .balign 0x40
	.global prog_a,prog_l,prog_s,prog_x,ebflags
prog_a:	.word 0
prog_l:	.word 0
prog_s:	.word 0
prog_x:	.word 0
	.extern prog_ext
	b prog_ext
ebflags:.byte 0x10	// LPUART1 at PZx
	.byte 0x1,0,0,0,0,0,0

        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b

        .balign 0x80
el1_curr_el_spx_sync:
1:	b	1b
        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b

        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b

        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b
        .balign 0x80
1:	b	1b

        .global  get_arm_tfreq,get_arm_time
get_arm_time:
	mrs x0,CNTPCT_EL0
	ret
get_arm_tfreq:
	mrs x0,CNTFRQ_EL0
	ret
	// coroutine support
	.globl coro_switch
coro_switch: // switch from x0 to x1
        stp     x0, x1, [sp, #-16]!
        stp     x19, x20, [sp, #-16]!
        stp     x21, x22, [sp, #-16]!
        stp     x23, x24, [sp, #-16]!
        stp     x25, x26, [sp, #-16]!
        stp     x27, x28, [sp, #-16]!
        stp     x29, x30, [sp, #-16]!
	mov	x2,sp
	str	x2, [x0]
	ldr	x2, [x1]
	mov	sp,x2
        ldp     x29, x30,[sp], #16
        ldp     x27, x28,[sp], #16
        ldp     x25, x26,[sp], #16
        ldp     x23, x24,[sp], #16
        ldp     x21, x22,[sp], #16
        ldp     x19, x20,[sp], #16
        ldp     x0, x1,[sp], #16
	ret
	.global call_smc,get_SP,get_VBAR1,mmu_on
call_smc:
	smc #0
	ret
get_SP:
	mov	x0, sp
	ret
get_VBAR1:
        mrs     x0,VBAR_EL1
	ret
mmu_on:
	IC IALLU	// Invalidate instruction cache
	DSB ISH		// Ensure completion of the invalidations
        mrs     x0, SCTLR_EL1
	mov	x1, #0x1005   // enable MMU & cache
	//mov	x1, #0x1  
        orr     x0, x0, x1
        msr     SCTLR_EL1, x0
	ISB 
	ret

