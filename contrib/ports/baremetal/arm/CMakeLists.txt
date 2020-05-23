include(${LWIP_DIR}/contrib/ports/CMakeCommon.cmake)

project(lwip-arm C CXX ASM)

set(LWIP_INCLUDE_DIRS
    "${LWIP_DIR}/src/include"
    "${LWIP_DIR}/src/include/lwip/apps"
    "${LWIP_DIR}/contrib/"
    "${LWIP_DIR}/contrib/examples/example_app"
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

list(APPEND LWIP_DEFINITIONS
    LWIP_MAIN_LOOP_INIT=stm32_init
)

set (MCU -mcpu=cortex-m3 -mthumb)

list(APPEND LWIP_COMPILER_FLAGS
    -Wno-c++-compat
    -Wno-c90-c99-compat
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-parameter
    -Wno-unused-but-set-variable
    -Wno-redundant-decls
    -fdata-sections
    -ffunction-sections
    ${MCU}
    $<$<CONFIG:Debug>:-gdwarf-2>
)

include(${LWIP_DIR}/src/Filelists.cmake)
include(${LWIP_DIR}/contrib/Filelists.cmake)

add_executable(example_app
    ${LWIP_DIR}/contrib/examples/example_app/test.c
    sys_arch.c
    sio.c
    STM32/Src/startup_stm32l151xe.s
    STM32/Src/main.c
    STM32/Src/uart.c
    STM32/Src/stm32l1xx_hal_msp.c
    STM32/Src/stm32l1xx_it.c
    STM32/Src/system_stm32l1xx.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_uart.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_tim.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_tim_ex.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_rcc.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_rcc_ex.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_flash.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_flash_ex.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_flash_ramfunc.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_gpio.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_dma.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_pwr.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_pwr_ex.c
    STM32/Drivers/STM32L1xx_HAL_Driver/Src/stm32l1xx_hal_cortex.c
)

target_include_directories(example_app PRIVATE
    ${LWIP_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/STM32/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/STM32/Drivers/STM32L1xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/STM32/Drivers/CMSIS/Device/ST/STM32L1xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/STM32/Drivers/CMSIS/Include
)
target_compile_options(example_app PRIVATE ${LWIP_COMPILER_FLAGS})

target_compile_definitions(example_app PRIVATE
    ${LWIP_DEFINITIONS}
    ${LWIP_MBEDTLS_DEFINITIONS}
    USE_HAL_DRIVER
    STM32L151xE
)
target_link_libraries(example_app ${LWIP_SANITIZER_LIBS} lwipcontribexamples lwipcontribapps lwipcontribaddons lwipallapps lwipcore lwipmbedtls)
target_link_libraries(example_app -lc -lm -lnosys)

target_link_options(example_app
    PRIVATE
    ${MCU}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32/Src/STM32L151RETx_FLASH.ld
    -specs=nano.specs
    -Wl,--gc-sections
)

# create binary & hex files and show size of resulting firmware image
add_custom_command(TARGET example_app POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:example_app> example_app.hex
        COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:example_app> example_app.bin
        COMMAND ${CMAKE_OBJDUMP} -S $<TARGET_FILE:example_app> > example_app.lss
        COMMAND ${CMAKE_SIZE} -B example_app
        COMMAND ${CMAKE_NM} -B -Crtd --size-sort example_app > example_app.size.txt
        COMMENT "Generating ${example_app.hex}, ${example_app.bin}")
