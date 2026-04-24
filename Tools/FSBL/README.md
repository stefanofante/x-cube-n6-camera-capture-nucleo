# FSBL — Load & Run per NUCLEO-N657X0-Q

Sotto-progetto CMake autonomo che compila un **First Stage Boot Loader**.
Tutti i sorgenti sono **vendored** nel repository: nessuna dipendenza da path
esterni, build riproducibile.

## Struttura

```
Tools/FSBL/
├── CMakeLists.txt
├── README.md
├── STM32N657XX_AXISRAM2_fsbl.ld   # linker script (AXISRAM2)
├── Inc/                           # header FSBL
│   ├── main.h
│   ├── extmem.h
│   ├── stm32_extmem_conf.h
│   ├── stm32n6xx_hal_conf.h       # HAL config specifico FSBL
│   └── stm32n6xx_it.h
├── Src/                           # sorgenti FSBL
│   ├── main.c                     # init clock/MPU, chiama EXTMEM_Boot
│   ├── extmem.c                   # config Octo-SPI / NOR SFDP
│   ├── stm32n6xx_hal_msp.c
│   ├── stm32n6xx_it.c
│   ├── system_stm32n6xx_fsbl.c    # CMSIS system file versione FSBL
│   ├── syscalls.c, sysmem.c       # stub newlib
└── Startup/
    └── startup_stm32n657xx_fsbl.s # vettori FSBL
```

Sorgenti **condivisi** con l'applicazione (stessa versione HAL, nessuna
duplicazione):

- `STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver/`
- `STM32Cube_FW_N6/Drivers/CMSIS/`
- `STM32Cube_FW_N6/Middlewares/ST/STM32_ExtMem_Manager/`

## Cosa fa al boot

1. La ROM ST copia l'FSBL firmato da Octo-SPI `0x70000000` ad AXISRAM2.
2. L'FSBL configura clock/PLL, cache, MPU, Octo-SPI memory-mapped.
3. Copia l'immagine app firmata (da `0x70100000`) in AXISRAM e salta
   all'entry point dell'applicazione ("Load & Run").

## Build

```powershell
cube-cmake -S Tools/FSBL -B Tools/FSBL/build -G Ninja `
           --toolchain cmake/gcc-arm-none-eabi.cmake
cube-cmake --build Tools/FSBL/build
```

Oppure da VS Code: `Terminal ▸ Run Task…` → **N6: Build FSBL**.

Output: `Tools/FSBL/build/FSBL.bin` (da firmare con `STM32_SigningTool_CLI`).
