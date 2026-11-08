ENTRY(reset_handler)
SECTIONS
{
    . = 0x08000000;
    .vectors : { KEEP(*(.vectors)) }
    .text : { *(.text*) *(.rodata*) }
}
