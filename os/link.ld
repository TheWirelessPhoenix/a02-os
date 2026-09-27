/* A02-OS RAM layout (ATJ2157, 224 KB @ 0x100000):
 *   0x100000-0x101fff  boot ROM stack/data (do not touch)
 *   0x102000-0x111fff  .bss / buffers (64 KB)
 *   0x112000-0x117fff  stack (24 KB, minimp3 needs ~16 KB)
 *   0x118000-0x11ffff  adfus loader + its buffers (do not touch while it must return)
 *   0x120000-0x137fff  code + rodata + data (96 KB)
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
	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(4);
		__bss_end = .;
	} > BSS
	__stack_top = ORIGIN(STK) + LENGTH(STK);
	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
