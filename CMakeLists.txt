cmake_minimum_required(VERSION 3.20)
project(geiger2_firmware C ASM)

if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi")
    message(FATAL_ERROR "Configure with the arm-none-eabi-gcc toolchain file.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(geiger2
    startup_stm32l051.S
    src/main.c
    src/board_stm32l051.c
    src/app.c
    src/app_config.c
    src/measurement.c
    src/rgb_cycle.c
    src/font_large.c
)
target_include_directories(geiger2 PRIVATE include)
target_compile_options(geiger2 PRIVATE
    -mcpu=cortex-m0plus -mthumb -ffunction-sections -fdata-sections
    -Wall -Wextra -Werror
)
target_link_options(geiger2 PRIVATE
    -mcpu=cortex-m0plus -mthumb -nostartfiles
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32L051C8TX_FLASH.ld
    -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/geiger2.map
)
set_target_properties(geiger2 PROPERTIES SUFFIX ".elf")

find_program(ARM_OBJCOPY arm-none-eabi-objcopy REQUIRED)
add_custom_command(TARGET geiger2 POST_BUILD
    COMMAND ${ARM_OBJCOPY} -O ihex $<TARGET_FILE:geiger2> ${CMAKE_CURRENT_BINARY_DIR}/geiger2.hex
    COMMAND ${ARM_OBJCOPY} -O binary $<TARGET_FILE:geiger2> ${CMAKE_CURRENT_BINARY_DIR}/geiger2.bin
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/geiger2.hex ${CMAKE_CURRENT_BINARY_DIR}/geiger2.bin
    COMMENT "Creating Intel HEX and binary images"
)
