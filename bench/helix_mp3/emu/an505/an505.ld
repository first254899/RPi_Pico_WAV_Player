/* QEMU mps2-an505 (Cortex-M33): everything in the 4 MB SSRAM at its secure alias 0x10000000 */
MEMORY { RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 4M }
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        __code_start = .;
        *(.text*)
        __code_end = .;
        __rodata_start = .;
        *(.rodata*)
        *(.flashdata*)
        __rodata_end = .;
        . = ALIGN(8);
    } > RAM
    .ARM.exidx : { *(.ARM.exidx*) } > RAM
    .data : { *(.data*) . = ALIGN(8); } > RAM
    .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end__ = .; } > RAM
    end = .;
    __end__ = .;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
