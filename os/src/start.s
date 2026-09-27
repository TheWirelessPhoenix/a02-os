@ A02-OS entry. Loaded to 0x120000 by actions_dump and entered at 0x120001 (Thumb).
@ Switches to our own stack, enables the FPU, zeroes .bss, calls main(), then returns to the
@ loader (adfus) with main's return value so the host can read a result block.
	.syntax unified
	.arch armv7e-m
	.fpu fpv4-sp-d16
	.thumb
	.section .text._start, "ax", %progbits
	.global _start
	.type _start, %function
_start:
	push	{r4-r7, lr}
	mov	r4, sp
	ldr	r0, =__stack_top
	mov	sp, r0
	push	{r4}
	sub	sp, sp, #4
	@ enable CP10/CP11 (FPU)
	ldr	r0, =0xe000ed88
	ldr	r1, [r0]
	orr	r1, r1, #(0xf << 20)
	str	r1, [r0]
	dsb
	isb
	@ zero .bss
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	movs	r2, #0
1:	cmp	r0, r1
	bhs	2f
	str	r2, [r0], #4
	b	1b
2:	bl	main
	add	sp, sp, #4
	pop	{r4}
	mov	sp, r4
	pop	{r4-r7, pc}
