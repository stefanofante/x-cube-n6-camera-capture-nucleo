/* Entry Point */
ENTRY(Reset_Handler)

/* The application copy must end before the running bootloader. NOLOAD DMA
 * storage can reuse the bootloader RAM after the final jump. */
_fsbl_start_address = 0x34180000;

/* Highest address of the user mode stack */
_estack = ORIGIN(APP_RAM) + LENGTH(APP_RAM); /* end of "AXISRAM2_P2_S" Ram type memory */
_sstack = _estack - _Min_Stack_Size;

_Min_Heap_Size = 0x200; /* required amount of heap  */
_Min_Stack_Size = 0x1000; /* required amount of stack */

/* Memories definition */
MEMORY
{
  APP_RAM (xrw)             : ORIGIN = 0x34000400, LENGTH = 447K
  DMA_RAM (rw)              : ORIGIN = 0x34070000, LENGTH = 1536K
  AXI_SRAM_UNCACHED (xrw)   : ORIGIN = 0x341f0000, LENGTH =   64K
}

/* Sections */
SECTIONS
{
  /* The startup code into "RAM" Ram type memory */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >APP_RAM

  /* The program code and other data into "RAM" Ram type memory */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    KEEP (*(.init))
    KEEP (*(.fini))
    KEEP (*(.keep_me))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >APP_RAM

  .gnu.sgstubs :
  {
    . = ALIGN(4);
    *(.gnu.sgstubs*)   /* Secure Gateway stubs */
    . = ALIGN(4);
  } >APP_RAM

  /* Constant data into "RAM" Ram type memory */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >APP_RAM

  .ARM.extab   : {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >APP_RAM

  .ARM : {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >APP_RAM

  .preinit_array     :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >APP_RAM

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >APP_RAM

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >APP_RAM

  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections into "RAM" Ram type memory */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */


  } >APP_RAM

  /* Uninitialized data section into "RAM" Ram type memory */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >APP_RAM

  /* Retain a debugger-readable fault record across a software reset. */
  .noinit (NOLOAD) : { *(.noinit*) } >APP_RAM

  /* User_heap_stack section, used to check that there is enough "RAM" Ram  type memory left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >APP_RAM

  /* Uncached AXI SRAM */
  .uncached_bss (NOLOAD) :
  {
    . = ALIGN(32);
    __uncached_bss_start__ = .;
    *(.uncached_bss)
    *(.uncached_bss*)
    . = ALIGN(32);
    __uncached_bss_end__ = .;
  } >AXI_SRAM_UNCACHED

  /* Internal DMA storage. NOLOAD prevents the signed image from including
   * these buffers or overwriting the FSBL while it copies the application. */
  .dma_bss (NOLOAD):
  {
    . = ALIGN(32);
    __dma_bss_start__ = .;
    *(.dma_bss*)
    *(.psram_bss*)
    . = ALIGN(32);
    __dma_bss_end__ = .;
  } >DMA_RAM

  /* Remove information from the compiler libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

/* Absolute, top-level assertions are evaluated against final addresses. */
ASSERT(ADDR(.isr_vector) == 0x34000400, "Application vector address mismatch")
ASSERT(ABSOLUTE(_edata) <= 0x34070000, "Application image exceeds load region")
ASSERT(ABSOLUTE(_end) + _Min_Heap_Size <= _sstack, "Application heap overlaps stack")
ASSERT(ORIGIN(APP_RAM) + LENGTH(APP_RAM) <= _fsbl_start_address, "Application copy overlaps FSBL")
ASSERT(SIZEOF(.dma_bss) <= LENGTH(DMA_RAM), "DMA storage exceeds internal SRAM")
ASSERT(SIZEOF(.uncached_bss) <= LENGTH(AXI_SRAM_UNCACHED), "USB storage exceeds internal SRAM")
ASSERT(ORIGIN(DMA_RAM) + LENGTH(DMA_RAM) == ORIGIN(AXI_SRAM_UNCACHED), "DMA and USB boundary mismatch")
ASSERT(ORIGIN(AXI_SRAM_UNCACHED) + LENGTH(AXI_SRAM_UNCACHED) == 0x34200000, "Storage exceeds SRAM1/2")
