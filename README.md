# x-cube-n6-camera-capture-nucleo

UVC (USB Video Class) camera-capture firmware for the **STM32 Nucleo‑N657X0‑Q**
board. The MCU acquires frames from an MIPI‑CSI2 image sensor, encodes them
with the on‑chip H.264 hardware video encoder and exposes the stream to the
host PC as a standard UVC webcam over USB High‑Speed.

This is a port / adaptation of the original ST example
[`x-cube-n6-camera-capture`](https://github.com/STMicroelectronics/x-cube-n6-camera-capture)
(DK board) to the **Nucleo‑N657X0‑Q**, with the following main differences:

- no external PSRAM: all buffers fit in the internal AXISRAM/DTCM;
- no LCD / touch: the only output path is the UVC USB stream;
- a fully **vendored** First Stage Boot Loader (FSBL) sub‑project so the
  firmware can actually be programmed into the onboard Octo‑SPI NOR and run
  standalone;
- CMake + Ninja + VS Code tasks for build / sign / flash (STM32CubeIDE not
  required, only STM32CubeProgrammer).

> Status: working prototype. Default stream is **480×272 H.264 @ 30 fps**
> (single stream, monochrome — see [Limitations](#limitations)).

---

## Hardware

| Item                  | Part                                             | Notes                                                        |
| --------------------- | ------------------------------------------------ | ------------------------------------------------------------ |
| MCU board             | **NUCLEO‑N657X0‑Q** (STM32N657X0HxQ, Cortex‑M55) | Onboard ST‑LINK/V3, user USB on CN13 (High‑Speed)            |
| External NOR          | **Macronix MX25UM51245G** (512 Mb Octo‑SPI)      | Onboard, used for FSBL + signed application                  |
| Camera                | **Sony IMX335** on MB1854 MIPI‑CSI2 daughterboard | 5 MP, 2‑lane MIPI, used at 2592×1940 raw then downsized      |
| (Alternative sensor)  | VD66GY / VD6G, VD55G1, VD1943                    | Camera middleware already supports them (untested on Nucleo) |
| Host                  | PC with any UVC‑capable app                       | Windows Camera, OBS, VLC, `ffplay`, …                        |

Nucleo boot pins must be set to **Flash boot** for standalone operation.

---

## What the firmware does

1. Powers up, configures the system clock (PLL1 → 800 MHz CPU) and the MPU.
2. Brings up the MIPI‑CSI2 receiver and the IMX335 sensor through the
   **Camera Middleware** (`Lib/Camera_Middleware`) + **ISP library**.
3. Captures frames through DCMIPP in YUY2 pixel format.
4. Feeds the raw luma plane to the **STM32 H.264 hardware encoder**
   (Verisilicon H1 IP, integrated in STM32N6). A static grey UV plane is
   inserted so the output stream is a valid YUV 4:2:0 bitstream (the image
   is monochrome — see [Limitations](#limitations)).
5. Packs the compressed H.264 NAL units into UVC Frame‑Based payloads and
   sends them to the host over USB High‑Speed.
6. Runs on FreeRTOS with a dedicated task for capture, encode and USB.

Resulting USB device: a standard UVC webcam advertising one stream, default
**480×272 H.264 @ 30 fps**.

A second, uncompressed (YUY2) stream is defined for the DK build but is
disabled on the Nucleo because there is not enough SRAM for a full 720p YUY2
frame buffer.

---

## Project layout

```
.
├── CMakeLists.txt              # main application build
├── CMakePresets.json
├── STM32N657xx_nucleo.ld       # app linker script (all‑RAM, Load & Run)
├── cmake/
│   └── gcc-arm-none-eabi.cmake # toolchain file
├── Inc/ , Src/                 # application code (app_*.c/h, main.c, …)
├── Gcc/Src/                    # GCC‑specific glue (syscalls, console, …)
├── Lib/
│   ├── Camera_Middleware/      # ST Camera middleware + sensor drivers + ISP
│   ├── FreeRTOS/               # FreeRTOS kernel sources
│   └── uvcl/                   # Lightweight UVC class stack
├── Middlewares/ST/usbx/        # Azure RTOS USBX (device)
├── STM32Cube_FW_N6/            # Single, frozen copy of the ST CubeN6 pack
│   ├── Drivers/                #   HAL + CMSIS + BSP (Nucleo + DK)
│   └── Middlewares/            #   H.264 encoder, EWL, ExtMem_Manager, …
├── Tools/
│   └── FSBL/                   # Vendored First Stage Boot Loader sub‑project
└── .vscode/
    ├── tasks.json              # build / sign / flash tasks
    └── launch.json             # debug configurations
```

All third‑party / ST code is **frozen** inside the repository: building the
project does **not** require any external pack, CubeMX project, or reference
to `C:\devel\STM32CubeN6`. Only the GCC toolchain and STM32CubeProgrammer
are external dependencies.

---

## Boot model (Load & Run)

The Nucleo‑N6 has **no internal flash**; code lives on the external Octo‑SPI
NOR. Execution uses the ST **Load & Run** scheme:

```
Octo‑SPI NOR (MX25UM51245G)              Internal memory
┌──────────────────────────────┐         ┌────────────────────────┐
│ 0x70000000  FSBL  (signed)   │ ──ROM──▶│ AXISRAM2   FSBL runs   │
│ 0x70100000  App   (signed)   │ ──FSBL─▶│ AXISRAM1/2 App runs    │
└──────────────────────────────┘         └────────────────────────┘
```

- The BootROM verifies the FSBL signature and loads it to AXISRAM2.
- The FSBL brings up the Octo‑SPI in memory‑mapped mode, copies the signed
  application to AXISRAM and jumps to it.
- Both binaries are signed with `STM32_SigningTool_CLI` (dummy keys, for
  development only).

The FSBL source tree is under `Tools/FSBL/` — see its own
[README](Tools/FSBL/README.md) for details.

---

## Toolchain

| Tool                                | Version tested             | Notes                                      |
| ----------------------------------- | -------------------------- | ------------------------------------------ |
| `arm-none-eabi-gcc`                 | 14.3.1 + ST patches        | Bundled with STM32CubeIDE / Cube extension |
| CMake                               | ≥ 3.22                     |                                            |
| Ninja                               | ≥ 1.10                     |                                            |
| **STM32CubeProgrammer**             | **2.22.0**                 | Required for sign + flash                  |
| VS Code + C/C++ ext.                | latest                     | Optional but integrates the provided tasks |
| `cube-cmake` wrapper                | from STM32 VS Code ext.    | Convenience wrapper that sets the path to the bundled GCC |

STM32CubeProgrammer is expected at the default install location:

```
C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/
  ├── STM32_Programmer_CLI.exe
  └── STM32_SigningTool_CLI.exe
```

The VS Code tasks accept an override path if it is installed elsewhere.

---

## Building

### VS Code (recommended)

1. Open the workspace folder.
2. Run the CMake **Configure** task (generates `build/Debug/`).
3. Run the CMake **Build** task → produces
   `build/Debug/x-cube-n6-camera-capture-nucleo.elf` and `.bin`.
4. Run task **N6: Configure FSBL** and **N6: Build FSBL** →
   produces `Tools/FSBL/build/FSBL.elf` and `FSBL.bin`.

### Command line

```powershell
# Application
cube-cmake -S . -B build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cube-cmake --build build\Debug

# FSBL
cube-cmake -S Tools\FSBL -B Tools\FSBL\build -G Ninja -DCMAKE_BUILD_TYPE=Release
cube-cmake --build Tools\FSBL\build
```

`cube-cmake` is a thin wrapper that exports the GCC bundled with the
STM32CubeIDE VS Code extension. Any `arm-none-eabi-gcc` on PATH works too
— just point the toolchain file at it.

---

## Programming the board

All flash/sign operations are wrapped as VS Code tasks
([.vscode/tasks.json](.vscode/tasks.json)) that call
`STM32_Programmer_CLI.exe` and `STM32_SigningTool_CLI.exe`. External
loader used is **`MX25UM51245G_STM32N6570-NUCLEO.stldr`**.

Tasks available:

| Task                                 | Purpose                                          |
| ------------------------------------ | ------------------------------------------------ |
| `N6: Configure FSBL`                 | CMake configure for the FSBL sub‑project         |
| `N6: Build FSBL`                     | Ninja build of `FSBL.bin`                        |
| `N6: Sign FSBL`                      | Produce a signed FSBL image                      |
| `N6: Sign App`                       | Produce a signed application image               |
| `N6: Program FSBL`                   | Flash signed FSBL at `0x70000000`                |
| `N6: Program App`                    | Flash signed application at `0x70100000`         |
| `N6: Build + Sign + Program App`     | One‑shot application update loop                 |
| `N6: Full Program (FSBL + App)`      | First‑time provisioning                          |
| `N6: Erase External Flash`           | Full chip erase via the external loader          |

Typical first‑time sequence:

```
N6: Build FSBL          →  N6: Sign FSBL   →  N6: Program FSBL
cmake build app         →  N6: Sign App    →  N6: Program App
```

After that, day‑to‑day updates only need **Build + Sign + Program App**.

---

## Using the webcam

1. After `Program App`, reset the board.
2. Plug the **user USB** cable (CN13, USB‑C on the Nucleo) into the PC.
3. A new UVC device **STM32 N6 Camera** (or similar) appears.
4. Open any UVC‑aware application and select the stream — you should see
   the live camera image.

`ffplay` example:

```powershell
ffplay -f dshow -i video="STM32 N6 Camera"
```

OBS, VLC and Windows Camera also work.

---

## Limitations

- **Monochrome output.** The H.264 encoder requires YUV 4:2:0 input. Only
  the luma plane is filled from the sensor; a static grey chroma plane is
  injected, so colour information is lost. This is a RAM/bandwidth
  trade‑off driven by the Nucleo having no external PSRAM — the DK build
  of the original project provides colour.
- **Single stream, 480×272 @ 30 fps.** Higher resolutions would need
  external PSRAM. Sensor is configured in binning/crop modes that the
  IMX335 driver already supports; a 1920×1080 YUY2 path would fit the
  encoder but not the current SRAM budget.
- **Signing keys are the ST dummy development keys.** Do not use this
  configuration for production devices.

---

## Credits and references

This project reuses and adapts code from several upstream ST projects; all
sources are vendored in this repository, each under its original license.

| Component                                           | Upstream                                                                                       | Where in this repo                               |
| --------------------------------------------------- | ---------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| Original camera‑capture example (DK)                | [STMicroelectronics/x-cube-n6-camera-capture](https://github.com/STMicroelectronics/x-cube-n6-camera-capture) | `Src/`, `Inc/`, `Lib/uvcl/`                      |
| STM32CubeN6 MCU package (HAL, CMSIS, BSP, FSBL ref.) | [STMicroelectronics/STM32CubeN6](https://github.com/STMicroelectronics/STM32CubeN6)            | `STM32Cube_FW_N6/` , `Tools/FSBL/`               |
| H.264 hardware encoder driver + EWL                 | `STMicroelectronics` Verisilicon H1 port (shipped inside STM32CubeN6 Middlewares/Third_Party) | `STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder`, `STM32Cube_FW_N6/Middlewares/ST/VideoEncoder_EWL` |
| Camera middleware + ISP library                     | [STMicroelectronics/stm32-camera-middleware](https://github.com/STMicroelectronics/stm32-camera-middleware) | `Lib/Camera_Middleware/`                         |
| USBX (Azure RTOS)                                   | Microsoft / Eclipse ThreadX                                                                   | `Middlewares/ST/usbx/`                           |
| FreeRTOS kernel                                     | [FreeRTOS.org](https://www.freertos.org)                                                      | `Lib/FreeRTOS/`                                  |
| External memory manager                             | ST `stm32-mw-extmem-manager` (from STM32CubeN6)                                               | `STM32Cube_FW_N6/Middlewares/ST/STM32_ExtMem_Manager` |

The FSBL (`Tools/FSBL/`) is derived from the
**`Template_FSBL_LRUN`** project in
`STM32CubeN6/Projects/NUCLEO-N657X0-Q/Templates/` and has been converted
to a standalone CMake build.

---

## License

Application‑specific files in `Src/`, `Inc/`, `Gcc/`, `cmake/`, `Tools/FSBL/`
are distributed under the same terms as the original ST example they derive
from — see [`Inc/LICENSE.md`](Inc/LICENSE.md).

Each vendored third‑party component keeps its original license file
alongside its sources (e.g. `Lib/Camera_Middleware/LICENSE.md`,
`STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/LICENSE.txt`, …).

---

## Contributing / Issues

This is a personal port published as‑is. Bug reports and PRs are welcome,
but keep in mind that upstream fixes belong in the original ST repositories
listed above.
