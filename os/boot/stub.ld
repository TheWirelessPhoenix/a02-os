/* Boot stub layout: loaded by BREC at 0x108000 as KER_INIT.BIN, entered at 0x108001. */
OUTPUT_FORMAT("elf32-littlearm")
ENTRY(_start)
SECTIONS {
	.text 0x108000 : { *(.text.entry) *(.text .text.*) *(.rodata .rodata.*) }
	ASSERT(. <= 0x108800, "stub code overlaps the original KER_INIT copy")
	.orig 0x108800 : { *(.orig) }
	ASSERT(SIZEOF(.orig) == 0xa00, "original KER_INIT must be 0xa00 bytes")
	.tramp 0x109200 : { *(.tramp) }
	.data : { *(.data .data.*) }
	.bss : { *(.bss .bss.* COMMON) }
	ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "stub must not use .data/.bss")
	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
