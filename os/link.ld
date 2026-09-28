/* A02-OS RAM layout (ATJ2157, 224 KB @ 0x100000):
 *   0x100000-0x101fff  boot ROM stack/data (do not touch)
 *   0x102000-0x111fff  .bss / buffers (64 KB)
 *   0x112000-0x117fff  stack (24 KB, minimp3 needs ~16 KB)
 *   0x118000-0x11ffff  adfus loader + its buffers (do not touch while it must return)
 *   0x120000-0x12ffff  code + rodata + data (64 KB; 0x130000 = ADFU-reboot magic word)
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
	CODE (rwx) : ORIGIN = 0x120000, LENGTH = 96K
	BSS  (rw)  : ORIGIN = 0x102000, LENGTH = 64K
	STK  (rw)  : ORIGIN = 0x112000, LENGTH = 24K
}

SECTIONS {
	.text : {
		*(.text._start)
		*(.text .text.*)
		*(.rodata .rodata.*)
		. = ALIGN(4);
		*(.data .data.*)
		. = ALIGN(4);
	} > CODE
	/* 0x130000 holds the ADFU-reboot magic (power.c, boot ROM): the image must end below it. */
	ASSERT(ADDR(.text) + SIZEOF(.text) <= 0x130000, "image overlaps the ADFU magic word at 0x130000")
	.bss (NOLOAD) : {
		__bss_start = .;
		/* Any payload's USB result block lands at a fixed 0x102000, so the host
		 * can always read_mem2 0x102000 without looking the address up. */
		*(.bss.result)
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > BSS
	__stack_top = ORIGIN(STK) + LENGTH(STK);
	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
