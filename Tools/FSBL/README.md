# NUCLEO-N657X0-Q Load & Run bootloader

This standalone CMake target uses the vendored STM32N6 HAL, CMSIS and external-memory manager. It maps the onboard MX25UM51245G NOR through XSPI2, validates a development STM32 v2.3 application image and copies its header and complete payload into internal SRAM.

From the repository root, with `arm-none-eabi-gcc`, CMake and Ninja on PATH:

```sh
cmake -S Tools/FSBL -B Tools/FSBL/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  --toolchain "$PWD/cmake/gcc-arm-none-eabi.cmake"
cmake --build Tools/FSBL/build --parallel 4
```

The outputs are `Tools/FSBL/build/FSBL.elf`, `FSBL.bin` and `FSBL.map`. Sign the unsigned binary with STM32CubeProgrammer's signing tool using `-t fsbl -hv 2.3 -nk`, then program the signed image at `0x70000000` using the Nucleo external loader. The main README describes the complete programming sequence.

The ROM-loaded FSBL vectors remain at `0x34180400`. The application image begins at `0x34000000`; its 1 KiB header puts the application vectors at `0x34000400`. Its entire signed image must fit below `0x34070000`, without overwriting the executing FSBL. Application DMA buffers may reuse FSBL RAM only after the jump; they are `NOLOAD` and are excluded from the copied application binary.

`boot_image.c` validates magic, header version, bounded payload length, the fixed stack boundary, a Thumb reset address inside the payload and the STM32 additive payload checksum. `boot_validation.c` overrides the middleware's weak fixed-length hook, replacing the former 64 KiB copy limit. Invalid images stop before any copy or jump. These development checks do not authenticate signatures and must not be presented as production secure boot.

Normal builds define `NO_OTP_FUSE`. Boot must not program irreversible OTP bits. Any required I/O high-speed provisioning is a separate physical-board procedure using the correct voltage and device documentation.

Run the host parser tests and `python3 Tools/check_layout.py` after building both targets. Signing with STM32CubeProgrammer and cold boot on the actual Nucleo remain required hardware validation steps; host tests use synthetic headers. There is no GitHub CI/CD.
