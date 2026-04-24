# ============================================================================
# FSBL (First Stage Boot Loader) - Load & Run per NUCLEO-N657X0-Q
# ============================================================================
# Sotto-progetto CMake autonomo. Tutti i sorgenti sono VENDORED dentro il
# repository: niente dipendenze da path esterni.
#
# Sorgenti:
#   Tools/FSBL/Src, Tools/FSBL/Inc, Tools/FSBL/Startup     : FSBL application
#   STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver           : HAL (condiviso con l'app)
#   STM32Cube_FW_N6/Drivers/CMSIS                          : CMSIS (condiviso)
#   STM32Cube_FW_N6/Middlewares/ST/STM32_ExtMem_Manager    : ExtMem Manager
#
# USO:
#   cmake -S Tools/FSBL -B Tools/FSBL/build -G Ninja \
#         --toolchain ../../cmake/gcc-arm-none-eabi.cmake
#   cmake --build Tools/FSBL/build
#
# Output:
#   Tools/FSBL/build/FSBL.elf
#   Tools/FSBL/build/FSBL.bin   <-- da firmare con STM32_SigningTool_CLI
# ============================================================================

cmake_minimum_required(VERSION 3.20)

project(FSBL C ASM)

# Root del repository (due livelli sopra Tools/FSBL)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(FSBL_DIR    "${CMAKE_CURRENT_SOURCE_DIR}")
set(HAL_ROOT    "${REPO_ROOT}/STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver")
set(CMSIS_ROOT  "${REPO_ROOT}/STM32Cube_FW_N6/Drivers/CMSIS")
set(EXTMEM_ROOT "${REPO_ROOT}/STM32Cube_FW_N6/Middlewares/ST/STM32_ExtMem_Manager")

set(LINKER_SCRIPT "${FSBL_DIR}/STM32N657XX_AXISRAM2_fsbl.ld")

add_executable(FSBL
    # ---- FSBL application ----
    "${FSBL_DIR}/Src/main.c"
    "${FSBL_DIR}/Src/extmem.c"
    "${FSBL_DIR}/Src/stm32n6xx_hal_msp.c"
    "${FSBL_DIR}/Src/stm32n6xx_it.c"
    "${FSBL_DIR}/Src/system_stm32n6xx_fsbl.c"
    "${FSBL_DIR}/Src/syscalls.c"
    "${FSBL_DIR}/Src/sysmem.c"
    "${FSBL_DIR}/Startup/startup_stm32n657xx_fsbl.s"
    # ---- STM32_ExtMem_Manager (shared middleware) ----
    "${EXTMEM_ROOT}/stm32_extmem.c"
    "${EXTMEM_ROOT}/boot/stm32_boot_lrun.c"
    "${EXTMEM_ROOT}/nor_sfdp/stm32_sfdp_driver.c"
    "${EXTMEM_ROOT}/nor_sfdp/stm32_sfdp_data.c"
    "${EXTMEM_ROOT}/sal/stm32_sal_xspi.c"
    # ---- HAL modules abilitati in Tools/FSBL/Inc/stm32n6xx_hal_conf.h ----
    "${HAL_ROOT}/Src/stm32n6xx_hal.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_bsec.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_cortex.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_dma.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_dma_ex.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_exti.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_gpio.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_pwr.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_pwr_ex.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_rcc.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_rcc_ex.c"
    "${HAL_ROOT}/Src/stm32n6xx_hal_xspi.c"
)

target_include_directories(FSBL PRIVATE
    "${FSBL_DIR}/Inc"
    "${EXTMEM_ROOT}"
    "${EXTMEM_ROOT}/boot"
    "${EXTMEM_ROOT}/nor_sfdp"
    "${EXTMEM_ROOT}/sal"
    "${HAL_ROOT}/Inc"
    "${HAL_ROOT}/Inc/Legacy"
    "${CMSIS_ROOT}/Device/ST/STM32N6xx/Include"
    "${CMSIS_ROOT}/Include"
)

target_compile_definitions(FSBL PRIVATE
    STM32N657xx
    USE_HAL_DRIVER
)

# Flag arch identici al main project (Cortex-M55 + FPU hard, secure)
target_compile_options(FSBL PRIVATE
    -mcpu=cortex-m55
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -mcmse
    -Os
    -ffunction-sections
    -fdata-sections
    -Wall
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11>
)

target_link_options(FSBL PRIVATE
    -mcpu=cortex-m55
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/FSBL.map
    --specs=nano.specs
    --specs=nosys.specs
    -static
)

set_target_properties(FSBL PROPERTIES
    SUFFIX ".elf"
    LINK_DEPENDS "${LINKER_SCRIPT}"
)

# Genera FSBL.bin accanto a FSBL.elf
add_custom_command(TARGET FSBL POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:FSBL>" "${CMAKE_BINARY_DIR}/FSBL.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:FSBL>"
    COMMENT "Generating FSBL.bin"
)
