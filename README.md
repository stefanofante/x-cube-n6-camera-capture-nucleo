# x-cube-n6-camera-capture-nucleo

UVC camera firmware for **NUCLEO-N657X0-Q**, using an IMX335 MIPI-CSI2 camera, DCMIPP/ISP processing, the STM32N657 hardware H.264 encoder and USB High-Speed output. The configured profile is **480 x 272 H.264 at a nominal 30 fps**.

The application orchestration uses C++20 with static classes, fixed-size frame pools and explicit ownership. HAL, CMSIS, camera middleware, FreeRTOS, USBX and the encoder implementation remain vendored C code. No STM32CubeIDE project, external Cube pack or external PSRAM is required to build.

This revision passes ARM builds, local sanitizer regression tests and linked-memory checks. Its revised boot, snapshot capture and streaming paths still require cold-boot and USB tests on the physical board. Performance improvements described below are implementation changes, not measured hardware speedup claims.

## Hardware and resource budget

| Resource | Board/device capability | Firmware use |
| --- | --- | --- |
| MCU | STM32N657X0, Cortex-M55 | Existing 800 MHz CPU configuration, hard-float, secure-only FreeRTOS |
| Internal AXI SRAM | SRAM1 and SRAM2 are 1 MiB each in the CMSIS device definitions | Only these two banks are used by this firmware's memory layout |
| Additional SRAM/DTCM | Present in the device memory map | Not required by this layout |
| External PSRAM | Not fitted on this Nucleo | Not used |
| External NOR | Onboard MX25UM51245G, 512 Mbit / 64 MiB | Signed FSBL at `0x70000000`, signed application at `0x70100000` |
| Camera | IMX335 on MB1854 MIPI-CSI2 daughterboard | Camera middleware, ISP and DCMIPP pipe 1 |
| Encoder | On-chip VENC/H.264 hardware | One instance, 768 KiB static linear DMA arena |
| USB | USB1 OTG HS and embedded PHY, user connector CN13 | UVC Frame-Based H.264, USBX standalone worker |
| Debug/programming | Onboard ST-LINK/V3 | External NOR loader through STM32CubeProgrammer |
| UART | USART1, PE5/PE6, 115200 baud | Bounded diagnostic ring and one low-priority transmit task |

Other sensors supported by the vendored camera middleware have not been validated with this revision. The application accepts the configured Nucleo H.264 profile; it does not enable the DK's larger or uncompressed profiles.

### Internal SRAM layout

All addresses below are secure aliases. Bounds are shared in `Inc/board_memory.h` and checked against the final ELF by `Tools/check_layout.py`.

| Region | Address range, upper bound exclusive | Capacity | Purpose |
| --- | --- | ---: | --- |
| Signed application header | `0x34000000` - `0x34000400` | 1 KiB | STM32 v2.3 header |
| Application RAM | `0x34000400` - `0x34070000` | 447 KiB | Vectors, code, constants, data, BSS, task metadata/stacks, newlib heap and main stack |
| Internal DMA storage | `0x34070000` - `0x341f0000` | 1536 KiB | Capture frames, compressed output and encoder arena |
| Uncached USB storage | `0x341f0000` - `0x34200000` | 64 KiB | USBX DMA pool and PCD state |

The DMA budget is 510 KiB for two YUY2 capture buffers, 128 KiB for two H.264 output buffers and 768 KiB for the encoder arena: **1406 KiB used, 130 KiB remaining**. The checked USB region uses about 33.25 KiB, leaving about 30.75 KiB. The application binary is approximately 306 KiB with the tested Debug configuration; BSS, heap and stack consume additional application RAM and are separately checked by the linker.

DMA and USB memory are non-cacheable and execute-never. Application code and metadata remain outside those MPU regions. There is no per-frame CPU cache maintenance requirement for these DMA buffers, and no speculative use of unavailable PSRAM or extra SRAM banks.

The boot ROM still loads the FSBL vectors at `0x34180400`. The FSBL copies the application header and payload into the lower application region, so it cannot overwrite itself during loading. DMA/USB storage may reuse FSBL RAM **only after the jump to the application**: those sections are `NOLOAD`, never included in the copied binary, and initialized or used only by the application.

## Boot and image validation

The FSBL uses the vendored Load & Run memory manager with a local override of its application-size hook. It reads the actual STM32 v2.3 payload length instead of copying a fixed 64 KiB. Validation checks:

- STM32 magic and header version;
- payload length, source bounds and the 448 KiB signed-image capacity;
- the fixed application stack boundary and a Thumb reset address within the loaded payload;
- the additive payload checksum before copying or jumping.

Invalid images stop boot. The application vectors must be at `0x34000400`; application and FSBL linker assertions and the layout checker enforce the corresponding memory contract. A successful compilation alone does not establish that an image has been signed or booted on the board.

These are **development integrity checks**, not signature authentication. The provided signing tasks use `-nk` and are not a production secure-boot configuration. Normal app and FSBL builds do not program OTP. Any required I/O high-speed OTP provisioning is a separate board procedure: check the actual I/O voltages, device documentation and STM32CubeProgrammer configuration before making irreversible changes.

## Optimization and robustness changes in this revision

- **Static C++20 orchestration:** `StreamController` owns stream transitions; `FramePool` carries explicit states, stream generations and FIFO acquisition tickets. There are no intrusive buffer lists or paired list/semaphore counters. Application control and DMA storage are static; the vendored encoder still allocates its software control structures through newlib.
- **Snapshot capture:** DCMIPP stops writing before a completed frame becomes Ready. Capture is rearmed only into a free buffer, so encoder reads cannot race a continuous DMA overwrite. The sensor may keep streaming while acquisition pauses under backpressure. Actual throughput and rearm latency must be measured on the board.
- **Camera lifecycle serialization:** one camera worker handles capture and ISP operations. A mutex makes initialization and teardown mutually exclusive with that worker. Stale completion generations are ignored, and old peripheral completion flags are cleared before a new stream starts.
- **Event-driven scheduling:** frame, stream and USB-slot notifications replace 1 ms polling. USB waits have a per-frame deadline, camera completion has a finite timeout, and recoverable camera failures receive at most three attempts per host request. The camera worker stops issuing new operations after a fault, allowing the controller to recover. A hardware stop failure halts rather than recycling memory still accessible to DMA.
- **Correct UVC packets:** payload-based packet counts cover every frame-size remainder without reading past valid data. Abort/restart clears packet indices. USB acceptance and stop detach ownership atomically; failed submissions receive no release callback and accepted submissions release exactly once. Disconnect follows the same cleanup path.
- **One fewer USB copy:** encoded bytes are copied directly into a USBX payload buffer, alongside the two-byte UVC header. The intermediate full-packet staging copy is removed. EOF and End-of-Header flags are set explicitly, and packet progress advances only after USBX accepts the payload.
- **H.264 recovery and timing:** pre-encode drops do not force unnecessary IDR frames. Encoded-but-unsent frames do force the next intra frame because the encoder reference chain advanced. Rate control uses captured millisecond intervals, including dropped-frame gaps, with wrap-safe bounded timing. Level 3.1 covers this profile; filler NAL padding includes valid headers and trailing bits.
- **Bounded encoder arena:** offset-based allocation checks capacity before forming pointers, aligns each allocation to 32 bytes, preserves a high-water mark and resets only after encoder teardown. Input structures are zero-initialized and API failures preserve encoder error codes.
- **Bounded diagnostics:** a 1 KiB ring feeds a low-priority UART task in 64-byte chunks with finite timeouts. Overflow is best-effort and counted in `console_dropped_bytes`. Fatal errors save file, line, reason and fault status in `app_fault_record`, without depending on UART, heap or scheduler locks. Fault handling and stack-overflow checks remain active with `NDEBUG`.
- **Build portability:** the C++ standard is selected by CMake, C/C++ share board/device feature definitions, unsupported ST complexity diagnostics are filtered after a compiler capability check, and the application `.bin` is generated automatically. No RTTI, exceptions, C++ heap operators or dynamic global constructors are required by the linked application.

Telemetry distinguishes encoding, accepted submissions and frames whose bytes have been staged into USBX. `usb_staged_fps` is **not** a measurement of frames received or decoded by the host. Reported rates use the actual elapsed RTOS interval. Telemetry also reports the encoder arena lifetime peak and the stream/camera task stack high-water free space in bytes. Frame pools and fault/log counters are debugger-visible. These runtime values still need to be observed on the board.

Optimization choices such as LTO, larger buffers, cacheable frame storage or additional vectorization are not enabled speculatively. They require a new resource budget and measurements using the same sensor settings, scene, bitrate and toolchain. MVE task context support remains disabled; the revision adds no MVE requirement.

## Build requirements and commands

| Tool | Requirement |
| --- | --- |
| Arm GNU toolchain | `arm-none-eabi-gcc` and `arm-none-eabi-g++`, C++20 library headers and newlib/nano support |
| CMake | 3.22 or newer for the supplied presets |
| Ninja | 1.10 or newer |
| Python | Python 3 for the ELF/layout checker |
| Host compiler | C11/C++20 compiler for local regression tests |
| STM32CubeProgrammer | 2.22.0 or compatible version with SigningTool v2.3 and the Nucleo NOR loader |

The revision was built using GCC ARM 14.2.1, CMake 3.31.6 and Ninja 1.12.1. The ST-patched 14.3.1 toolchain documented by the original port remains an alternative requiring its own validation. Frozen third-party sources are included; no network package fetch is part of CMake configuration.

From the repository root, with the Arm tools on PATH:

```sh
# Application: configure the complete device/feature preset, then build.
cmake --preset Debug
cmake --build --preset Debug --parallel 4

# FSBL: select the Arm toolchain explicitly, never the host compiler.
cmake -S Tools/FSBL -B Tools/FSBL/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  --toolchain "$PWD/cmake/gcc-arm-none-eabi.cmake"
cmake --build Tools/FSBL/build --parallel 4

# Validate final artifacts and the board resource contract.
python3 Tools/check_layout.py
```

Outputs:

- `build/Debug/x-cube-n6-camera-capture-nucleo.elf`, `.bin`, `.map` and `compile_commands.json`;
- `Tools/FSBL/build/FSBL.elf`, `FSBL.bin` and `FSBL.map`.

`cube-cmake` can supply the ST toolchain PATH, but is not required. For cloud tasks using the prepared local tool installation, run `source /workspace/cloud-tools/activate.sh` before these commands. Build directories are ignored; generated firmware is not committed.

## Local regression tests

Tests run explicitly on the host and do not require a camera, USB connection or signing tool:

```sh
cmake -S tests -B build/host-tests -G Ninja
cmake --build build/host-tests --parallel 4
ctest --test-dir build/host-tests --output-on-failure
```

AddressSanitizer and UndefinedBehaviorSanitizer are enabled by default for supported GCC/Clang hosts. Disable them only when the local host cannot run those tools, using `-DENABLE_SANITIZERS=OFF`; that does not count as sanitizer validation.

The suites cover pool generations, duplicate/stale release, FIFO order, arena alignment/overflow, H.264 padding and timing, 131,072 packet-size cases, malformed synthetic boot headers and the actual UVC submit/stop/abort functions. Minimal HAL declarations let hardware-independent UVC functions run on the host; they do not simulate peripheral timing or interrupts.

Checks with assertions disabled:

```sh
cmake -S tests -B build/host-ndebug -G Ninja \
  -DCMAKE_C_FLAGS=-DNDEBUG -DCMAKE_CXX_FLAGS=-DNDEBUG
cmake --build build/host-ndebug --parallel 4
ctest --test-dir build/host-ndebug --output-on-failure

cmake --preset Debug -B build/ndebug \
  -DCMAKE_C_FLAGS=-DNDEBUG -DCMAKE_CXX_FLAGS=-DNDEBUG
cmake --build build/ndebug --parallel 4
python3 Tools/check_layout.py \
  --app build/ndebug/x-cube-n6-camera-capture-nucleo.elf
```

Both ARM configurations and all three host suites have passed. The layout checker also confirms vector/stack placement, `NOLOAD` DMA sections, exclusion of OTP programming and absence of unwanted C++ runtime dependencies. Legacy warnings in vendored C middleware remain visible and are not evidence of passing hardware validation.

## Sign and program the board

Set the Nucleo boot pins for Flash boot. Use STM32CubeProgrammer and the external loader **`MX25UM51245G_STM32N6570-NUCLEO.stldr`**. Build both images first, sign them with STM32 header version 2.3, then program:

| Image | Signing options | NOR address |
| --- | --- | --- |
| `Tools/FSBL/build/FSBL.bin` | `-t fsbl -hv 2.3 -nk` | `0x70000000` |
| Application `.bin` | `-t ssbl -hv 2.3 -nk` | `0x70100000` |

The VS Code tasks in `.vscode/tasks.json` configure/build before signing and allow programmer, signing tool and external-loader paths to be selected. Default executable paths are Windows STM32CubeProgrammer locations; override them on other hosts. `N6: Full Program (FSBL + App)` signs and programs both components in sequence. When changing the memory layout, update **both** FSBL and application on the board.

After reset, connect the user USB port and select the UVC camera in a compatible host application such as OBS, VLC or ffplay. The current software passes YUYV input directly to the hardware encoder; it does not create a static grey chroma plane. Actual color, decoder compatibility and stream timing require observation on the board.

### Required physical-board validation

Before calling the revised firmware operational, verify:

1. SigningTool output really contains a v2.3 1 KiB header with the expected payload length and checksum; host parser tests use synthetic envelopes.
2. Cold boot without a debugger, full-image loading and the new application vectors/stack.
3. Repeated start/stop and USB unplug/reset during capture, encode and payload transmission, with pool ownership reconciled afterward.
4. Missing sensor, stalled capture, encoder output overflow and UART saturation, including recovery deadlines and debugger-readable fault records.
5. H.264 parsing and decoding on at least two independent host decoders, including IDR recovery after dropped encoded frames.
6. Sustained measured throughput, capture-to-host latency, worst-case ISR duration, task stack high-water marks and encoder arena peak usage.

These checks were not executed in the cloud. No signing, flashing, OTP writes or measured hardware performance result is implied by the successful local builds and tests.

## Source layout and licenses

- `Src/app.cpp`: static stream controller, camera worker and buffer ownership;
- `Src/app_enc.cpp`: encoder configuration, timestamped frames and bounded DMA allocation;
- `Src/app_cam.c`: camera/ISP HAL adaptation with explicit status returns;
- `Inc/static_storage.hpp`: host-testable fixed pools, timing and arena utilities;
- `Lib/uvcl`: UVC packetization and USBX integration;
- `Tools/FSBL`: bootloader and image validation;
- `Tools/check_layout.py`, `tests`: explicit local verification;
- `STM32Cube_FW_N6`, `Lib/Camera_Middleware`, `Lib/FreeRTOS`: frozen upstream components.

This project derives from [STMicroelectronics/x-cube-n6-camera-capture](https://github.com/STMicroelectronics/x-cube-n6-camera-capture), [STM32CubeN6](https://github.com/STMicroelectronics/STM32CubeN6) and [stm32-camera-middleware](https://github.com/STMicroelectronics/stm32-camera-middleware), together with USBX, FreeRTOS and the vendored Verisilicon encoder. Application-derived code follows the terms in [Inc/LICENSE.md](Inc/LICENSE.md); each third-party component retains its own license and copyright notices.
