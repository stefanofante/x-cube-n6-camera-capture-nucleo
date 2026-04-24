# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.20)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Project definition
project(x-cube-n6-camera-capture-nucleo)

# Enable CMake support for ASM and C/CPP languages
enable_language(C ASM)

set(BUILD_UNIT_0_NAME x-cube-n6-camera-capture-nucleo)

# Create an executable object type
add_executable(${BUILD_UNIT_0_NAME})

# STM32CubeIDE for Visual Studio Code managed CMake file
include(cmake/vscode_generated.cmake)

# GCC 14 newlib-nano now provides syscall stubs in libraries; keep project stubs
# and drop nosys to avoid duplicate symbol definitions at link time.
get_target_property(_link_opts ${BUILD_UNIT_0_NAME} LINK_OPTIONS)
if(_link_opts)
  list(FILTER _link_opts EXCLUDE REGEX "^--specs=nosys\\.specs$")
  list(FILTER _link_opts EXCLUDE REGEX "^-lnosys$")
  set_property(TARGET ${BUILD_UNIT_0_NAME} PROPERTY LINK_OPTIONS "${_link_opts}")
endif()

# Compiler options
target_compile_options(${BUILD_UNIT_0_NAME} PRIVATE
  # User defined compiler options
)

# Linker options
target_link_options(${BUILD_UNIT_0_NAME} PRIVATE 
  # User defined linker options
)

# Add sources to executable/library
target_sources(${BUILD_UNIT_0_NAME} PRIVATE
  # H264 encoder application API
  Src/app_enc.c
  # Low-Level VENC driver (IP clock / reset / init)
  STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_venc.c
  # EWL (Encoder Wrapper Layer) — ST implementation on top of VENC IP
  STM32Cube_FW_N6/Middlewares/ST/VideoEncoder_EWL/ewl_impl.c
  # HANTRO H1 (VC8000NanoE) encoder — common
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encasiccontroller.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encasiccontroller_v2.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encInputLineBuffer.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encpreprocess.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encswhwregisters.c
  # HANTRO H1 — H264 path
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Cabac.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264CodeFrame.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Denoise.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/h264encapi_ext.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264EncApi.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Init.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Mad.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264NalUnit.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264PictureBuffer.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264PictureParameterSet.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264PutBits.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264RateControl.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Sei.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264SequenceParameterSet.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Slice.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264TestId.c
)

# Add include directories
target_include_directories(${BUILD_UNIT_0_NAME} PRIVATE
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/inc
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264
  STM32Cube_FW_N6/Middlewares/ST/VideoEncoder_EWL
)

# HANTRO H1 sources have a number of legacy K&R-era type mismatches (e.g.
# const int (*)[N][2] vs const i32 (*)[N][2]) that modern GCC flags as errors.
# Relax those specific diagnostics for the encoder tree only.
set_source_files_properties(
  STM32Cube_FW_N6/Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_ll_venc.c
  STM32Cube_FW_N6/Middlewares/ST/VideoEncoder_EWL/ewl_impl.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encasiccontroller.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encasiccontroller_v2.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encInputLineBuffer.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encpreprocess.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/common/encswhwregisters.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Cabac.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264CodeFrame.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Denoise.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/h264encapi_ext.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264EncApi.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Init.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Mad.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264NalUnit.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264PictureBuffer.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264PictureParameterSet.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264PutBits.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264RateControl.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Sei.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264SequenceParameterSet.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264Slice.c
  STM32Cube_FW_N6/Middlewares/Third_Party/VideoEncoder/source/h264/H264TestId.c
  PROPERTIES COMPILE_OPTIONS "-Wno-incompatible-pointer-types;-Wno-implicit-function-declaration;-Wno-int-conversion"
)

# Exclude the old MJPEG path from the auto-generated source list.
# `cmake/vscode_generated.cmake` lists Src/app_jpg.c and Src/app_cvt.c; both
# are now replaced by the H264 encoder path driven by app_enc.c.
get_target_property(_srcs ${BUILD_UNIT_0_NAME} SOURCES)
if(_srcs)
  list(FILTER _srcs EXCLUDE REGEX "Src/app_jpg\\.c$")
  set_property(TARGET ${BUILD_UNIT_0_NAME} PROPERTY SOURCES "${_srcs}")
endif()
