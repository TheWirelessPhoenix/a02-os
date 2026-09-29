@ Boot stub entry (0x108000), stock trampoline and A02-OS hand-off. See stub.c.
	.syntax unified
	.arch armv7e-m
	.thumb

	.section .text.entry, "ax", %progbits
	.global _start
	.type _start, %function
_start:					@ called by BREC: blx 0x108001, r0 = *(u32 *)0
	push	{r0-r12, lr}
	ldr	r0, =saved_sp
	str	sp, [r0]
	ldr	r1, =0xe000e010		@ remember SysTick (the stub uses it; stock gets it back)
	ldr	r2, [r1]
	str	r2, [r0, #4]
	ldr	r2, [r1, #4]
	str	r2, [r0, #8]
	bl	stub_main		@ never returns

	.section .tramp, "ax", %progbits	@ 0x109200: never overwritten by the stock copy
	.global stock_boot
	.type stock_boot, %function
stock_boot:				@ put the original KER_INIT back at 0x108000 and run it
	ldr	r0, =orig_ker_init
	ldr	r1, =0x108000
	ldr	r2, =(0xa00 / 4)
1:	ldr	r3, [r0], #4
	str	r3, [r1], #4
	subs	r2, r2, #1
	bne	1b
	dsb
	isb
	ldr	r0, =saved_sp
	ldr	r1, =0xe000e010		@ SysTick back as BREC left it
	movs	r2, #0
	str	r2, [r1]
	ldr	r2, [r0, #8]
	str	r2, [r1, #4]
	movs	r2, #0
	str	r2, [r1, #8]
	ldr	r2, [r0, #4]
	str	r2, [r1]
	ldr	sp, [r0]
	pop	{r0-r12, lr}		@ exactly as BREC called us
	ldr	r12, =0x108001
	bx	r12

	.global enter_a02os
	.type enter_a02os, %function
enter_a02os:
	cpsid	i
	ldr	r0, =0xe000e180		@ NVIC ICER0..7 / ICPR0..7: nothing can interrupt A02-OS
	mvn	r1, #0
	movs	r2, #8
2:	str	r1, [r0], #4
	subs	r2, r2, #1
	bne	2b
	ldr	r0, =0xe000e280
	movs	r2, #8
3:	str	r1, [r0], #4
	subs	r2, r2, #1
	bne	3b
	ldr	r0, =0xe000e010		@ SysTick off
	movs	r1, #0
	str	r1, [r0]
	ldr	r0, =0xc003001c		@ watchdog off (as nandhwsc / USB recovery leave it)
	str	r1, [r0]
	ldr	r0, =fallback_start	@ copy the fallback to 0x11fe00 (A02-OS never touches 0x118000-0x11ffff)
	ldr	r1, =0x11fe00
	ldr	r2, =fallback_end
4:	ldr	r3, [r0], #4
	str	r3, [r1], #4
	cmp	r0, r2
	blo	4b
	dsb
	isb
	ldr	r0, =0x11ff00
	mov	sp, r0
	ldr	lr, =0x11fe01		@ if A02-OS's main ever returns: USB recovery
	ldr	r0, =0x120001
	bx	r0
	.ltorg

	.balign 4
fallback_start:				@ position-independent: ADFU magic + watchdog reset
	movw	r0, #0x0000
	movt	r0, #0x0013
	movw	r1, #0xadf0
	movt	r1, #0xadf0
	str	r1, [r0]
	movw	r0, #0x001c
	movt	r0, #0xc003
	movs	r1, #0x5f
	str	r1, [r0]
5:	b	5b
	.balign 4
fallback_end:

	.balign 4
	.global saved_sp
saved_sp:
	.word	0		@ +0 BREC's sp, +4 SYST_CSR, +8 SYST_RVR
	.word	0
	.word	0

	.section .orig, "a", %progbits	@ 0x108800: original KER_INIT, padded to its LFI size
	.global orig_ker_init
orig_ker_init:
	.incbin	"KER_INIT.BIN"
	.fill	0xa00 - (. - orig_ker_init), 1, 0
