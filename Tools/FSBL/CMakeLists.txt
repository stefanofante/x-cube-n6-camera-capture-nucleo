# Standalone Load & Run bootloader for NUCLEO-N657X0-Q.
# HAL, CMSIS and the memory manager are vendored and shared with the app.
# Configure from the repository root with the explicit Arm GCC toolchain.
cmake_minimum_required(VERSION 3.20)

project(FSBL C ASM)

# Repository root, two levels above Tools/FSBL.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(FSBL_DIR    "${CMAKE_CURRENT_SOURCE_DIR}")
set(HAL_ROOT    "${REPO_ROOT}/STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver")
set(CMSIS_ROOT  "${REPO_ROOT}/STM32Cube_FW_N6/Drivers/CMSIS")
set(EXTMEM_ROOT "${REPO_ROOT}/STM32Cube_FW_N6/Middlewares/ST/STM32_ExtMem_Manager")

set(LINKER_SCRIPT "${FSBL_DIR}/STM32N657XX_AXISRAM2_fsbl.ld")

add_executable(FSBL
    # ---- FSBL application ----
    "${FSBL_DIR}/Src/main.c"
    "${FSBL_DIR}/Src/boot_image.c"
    "${FSBL_DIR}/Src/boot_validation.c"
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
    # HAL modules enabled by the FSBL configuration.
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
    "${REPO_ROOT}/Inc"
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

# Match the application Cortex-M55 hard-float and secure architecture.
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

# Produce the unsigned binary next to FSBL.elf for the signing step.
add_custom_command(TARGET FSBL POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:FSBL>" "${CMAKE_BINARY_DIR}/FSBL.bin"
    COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:FSBL>"
    COMMENT "Generating FSBL.bin"
)

# Normal builds never program OTP. Provisioning is an explicit board operation.
target_compile_definitions(FSBL PRIVATE NO_OTP_FUSE)
