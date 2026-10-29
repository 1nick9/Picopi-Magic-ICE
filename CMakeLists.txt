cmake_minimum_required(VERSION 3.13)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# ---------------------------------------------------------------------------
# PS2 Magic ICE Final / H2O-Improved  ->  RP2040 port
#
# Board (pin map in include/boards/ice_board_<board>.h):
#   VS Code Pico extension: change the default in the ICE_BOARD line below,
#   then "Clean CMake" (or delete the build folder) and compile again.
#
#   -DICE_BOARD=pico                    Raspberry Pi Pico (default)
#   -DICE_BOARD=waveshare_rp2040_tiny   Waveshare RP2040-Tiny
#   -DICE_BOARD=yd_rp2040               VCC-GND YD-RP2040
#        flash size: edit YD_RP2040_FLASH_MB in boards/yd_rp2040.h (4 or 16)
#        or pass -DYD_RP2040_FLASH_MB=4 / 16 here
#
# Build-time options (mirror the IFDEF switches of the SX28 source):
#
#   -DMECH_OE_MODE=DIRECT     mechacon OE ("A") wired straight to a GPIO; the
#                             PIO latches the edge itself (no 74xx flip-flop)
#   -DMECH_OE_MODE=FLIPFLOP   original Magic ICE board: A -> flip-flop CLK,
#                             /1Q -> MECH_FF_Q_PIN, /1R <- MECH_FF_R_PIN
#
#   -DRSTBUMP=ON/OFF          RSTBUMP: auto-reset into PS1 mode drives RESET
#                             directly.  OFF (default) = "F=TR": pad F drives a
#                             2N7002 that pulls RESET
#   -DH2O75KJMPERS=ON/OFF     region ident jumpers on F/H (USA/JAP v14, v8 JAP).
#                             Default OFF.  Not with F=TR.
#   -DICE_FIXED_REGION=JAPv14orv8   F=TR builds: fixed JAP 75k region, and v7/v8
#                             JAP consoles take the v9+ mechacon patch
#   -DICE_FIXED_REGION=USAv14 F=TR builds: fixed USA 75k region
#   -DNTSCPS1YFIX75K=ON/OFF   NTSCPS1YFIX75K (75k PAL console: NTSC PS1 Y-fix)
#   -DICEFINALDEV1=ON/OFF     OFF = H2O dev1 patch (115 bytes, default here)
#                             ON  = ICE Final dev1 patch (119 bytes)
#
#   -DBIOS_QUALIFY=OE_WIDTH|NONE   drop <80 ns /OE strobes (default) or not
#   -DICE_MECH_PATCH=OFF      test build: never drive the mechacon bus
#                             (or "#define MECH_PATCH 0" in include/config.h)
#   -DMECH_WRITE_DELAY_NS=10  DIRECT: put patch nibbles on the bus 10 ns after
#                             A falls instead of at the read point
#   -DICE_DEBUG=ON/OFF        USB serial debug log (core 1 = modchip,
#                             core 0 = USB).  Default ON.
#
# Defaults = the SX28 "F=TR" build: RSTBUMP off, H2O75KJMPERS off,
# NTSCPS1YFIX75K on, H2O dev1 patch.
# ---------------------------------------------------------------------------

set(ICE_BOARD "pico" CACHE STRING "Target board: pico, waveshare_rp2040_tiny, yd_rp2040")
set_property(CACHE ICE_BOARD PROPERTY STRINGS pico waveshare_rp2040_tiny yd_rp2040)
set(YD_RP2040_FLASH_MB "" CACHE STRING "YD-RP2040 flash in MB (empty = value in boards/yd_rp2040.h)")

set(MECH_OE_MODE "DIRECT" CACHE STRING "Mechacon OE input: DIRECT or FLIPFLOP")
set_property(CACHE MECH_OE_MODE PROPERTY STRINGS DIRECT FLIPFLOP)
set(BIOS_QUALIFY "OE_WIDTH" CACHE STRING "BIOS strobe filter: OE_WIDTH or NONE (CE is never used, as in the SX28)")
set_property(CACHE BIOS_QUALIFY PROPERTY STRINGS OE_WIDTH NONE)
option(RSTBUMP        "Auto-reset into PS1 mode drives RESET (RSTBUMP)" OFF)
option(H2O75KJMPERS   "Region ident jumpers on F/H (H2O75KJMPERS)"   OFF)
set(ICE_FIXED_REGION "" CACHE STRING "F=TR region define: empty, JAPv14orv8 or USAv14")
set_property(CACHE ICE_FIXED_REGION PROPERTY STRINGS "" JAPv14orv8 USAv14)
option(NTSCPS1YFIX75K "NTSC PS1 Y-fix on 75k PAL console"            ON)
option(ICEFINALDEV1   "Use ICE Final dev1 patch instead of H2O dev1"  OFF)
option(ICE_DEBUG      "USB serial debug output"                      ON)
set(ICE_MECH_PATCH "" CACHE STRING "OFF = test build, never drive the mechacon bus (empty = value in config.h)")
set(MECH_WRITE_DELAY_NS "" CACHE STRING "DIRECT writer delay after A falls in ns (empty = config.h, = sample delay)")
set(SYS_CLOCK_KHZ "240000" CACHE STRING "RP2040 system clock in kHz (240000 like fruitchip; PIO delays are in ns and follow it)")

# ---- board -> SDK board header ---------------------------------------------
if(ICE_BOARD STREQUAL "pico")
    set(PICO_BOARD pico CACHE STRING "" FORCE)
    set(ICE_BOARD_DEF ICE_BOARD_PICO)
elseif(ICE_BOARD STREQUAL "waveshare_rp2040_tiny")
    set(PICO_BOARD waveshare_rp2040_tiny CACHE STRING "" FORCE)
    set(ICE_BOARD_DEF ICE_BOARD_WAVESHARE_RP2040_TINY)
elseif(ICE_BOARD STREQUAL "yd_rp2040")
    # boards/yd_rp2040.h is not part of the SDK
    list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
    set(PICO_BOARD yd_rp2040 CACHE STRING "" FORCE)
    set(ICE_BOARD_DEF ICE_BOARD_YD_RP2040)
    if(NOT YD_RP2040_FLASH_MB STREQUAL "")
        math(EXPR _yd_bytes "${YD_RP2040_FLASH_MB} * 1024 * 1024")
        set(PICO_FLASH_SIZE_BYTES ${_yd_bytes})
    else()
        file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/boards/yd_rp2040.h _yd_line
             REGEX "^#define YD_RP2040_FLASH_MB[ \t]+[0-9]+")
        string(REGEX MATCH "[0-9]+$" _yd_mb "${_yd_line}")
        math(EXPR _yd_bytes "${_yd_mb} * 1024 * 1024")
        set(PICO_FLASH_SIZE_BYTES ${_yd_bytes})
    endif()
else()
    message(FATAL_ERROR "ICE_BOARD must be pico, waveshare_rp2040_tiny or yd_rp2040")
endif()

# ---- find the SDK / toolchain when neither the VS Code extension block above
#      nor PICO_SDK_PATH provided them (plain "cmake" from a terminal) --------
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    file(GLOB _ice_sdks LIST_DIRECTORIES true "${USERHOME}/.pico-sdk/sdk/*")
    if(_ice_sdks)
        list(SORT _ice_sdks COMPARE NATURAL ORDER DESCENDING)
        list(GET _ice_sdks 0 _ice_sdk)
        set(PICO_SDK_PATH ${_ice_sdk})
        message(STATUS "ps2-ice-pico: using SDK found at ${PICO_SDK_PATH}")
    endif()
endif()
if(NOT DEFINED PICO_TOOLCHAIN_PATH AND NOT DEFINED ENV{PICO_TOOLCHAIN_PATH})
    find_program(_ice_gcc arm-none-eabi-gcc)
    if(NOT _ice_gcc)
        file(GLOB _ice_tcs LIST_DIRECTORIES true "${USERHOME}/.pico-sdk/toolchain/*")
        if(_ice_tcs)
            list(SORT _ice_tcs COMPARE NATURAL ORDER DESCENDING)
            list(GET _ice_tcs 0 _ice_tc)
            set(PICO_TOOLCHAIN_PATH ${_ice_tc})
            message(STATUS "ps2-ice-pico: using toolchain found at ${PICO_TOOLCHAIN_PATH}")
        endif()
    endif()
endif()

include(pico_sdk_import.cmake)

project(ps2_ice_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(ps2_ice_pico
    src/main.c
    src/bios_bus.c
    src/mech_bus.c
    src/scex.c
    src/bios_ident.c
    src/patch_tables.c
    src/macro_patch.c
    src/osdsys_patch.c
    src/ps2logo_patch.c
    src/xcdvdman.c
    src/ps1_patch.c
    src/dev1.c
    src/media_patch.c
    src/console_io.c
    src/debug_log.c
)

pico_generate_pio_header(ps2_ice_pico ${CMAKE_CURRENT_LIST_DIR}/pio/bios_bus.pio)
pico_generate_pio_header(ps2_ice_pico ${CMAKE_CURRENT_LIST_DIR}/pio/mech_bus.pio)

target_include_directories(ps2_ice_pico PRIVATE include)

target_compile_definitions(ps2_ice_pico PRIVATE ${ICE_BOARD_DEF}=1 SYS_CLOCK_KHZ=${SYS_CLOCK_KHZ})
if(ICE_BOARD STREQUAL "yd_rp2040" AND NOT YD_RP2040_FLASH_MB STREQUAL "")
    target_compile_definitions(ps2_ice_pico PRIVATE YD_RP2040_FLASH_MB=${YD_RP2040_FLASH_MB})
endif()

if(MECH_OE_MODE STREQUAL "FLIPFLOP")
    target_compile_definitions(ps2_ice_pico PRIVATE MECH_OE_FLIPFLOP=1)
else()
    target_compile_definitions(ps2_ice_pico PRIVATE MECH_OE_DIRECT=1)
endif()
target_compile_definitions(ps2_ice_pico PRIVATE BIOS_QUALIFY=BIOS_QUALIFY_${BIOS_QUALIFY})
# the SX28 sources use IFDEF: OFF must mean "not defined" / 0
foreach(opt RSTBUMP H2O75KJMPERS NTSCPS1YFIX75K)
    if(${opt})
        target_compile_definitions(ps2_ice_pico PRIVATE ${opt}=1)
    else()
        target_compile_definitions(ps2_ice_pico PRIVATE ${opt}=0)
    endif()
endforeach()
if(NOT ICE_MECH_PATCH STREQUAL "")
    if(ICE_MECH_PATCH)
        target_compile_definitions(ps2_ice_pico PRIVATE MECH_PATCH=1)
    else()
        target_compile_definitions(ps2_ice_pico PRIVATE MECH_PATCH=0)
    endif()
endif()
if(NOT MECH_WRITE_DELAY_NS STREQUAL "")
    target_compile_definitions(ps2_ice_pico PRIVATE MECH_WRITE_DELAY_NS=${MECH_WRITE_DELAY_NS})
endif()
if(NOT ICE_FIXED_REGION STREQUAL "")
    if(NOT ICE_FIXED_REGION STREQUAL "JAPv14orv8" AND NOT ICE_FIXED_REGION STREQUAL "USAv14")
        message(FATAL_ERROR "ICE_FIXED_REGION must be empty, JAPv14orv8 or USAv14")
    endif()
    if(H2O75KJMPERS)
        message(FATAL_ERROR "ICE_FIXED_REGION is for F=TR builds (H2O75KJMPERS=OFF)")
    endif()
    target_compile_definitions(ps2_ice_pico PRIVATE ${ICE_FIXED_REGION}=1)
endif()
if(ICEFINALDEV1)
    target_compile_definitions(ps2_ice_pico PRIVATE ICEFINALDEV1=1)
endif()

target_compile_options(ps2_ice_pico PRIVATE -Wall -Wextra -O2)

# developer check: compile every DBG() as printf so gcc verifies the format
# string against the arguments (the binary does not link in this mode)
option(ICE_FORMAT_CHECK "Check DBG() formats with -Wformat (compile only)" OFF)
if(ICE_FORMAT_CHECK)
    target_compile_definitions(ps2_ice_pico PRIVATE ICE_DEBUG_FORMAT_CHECK=1)
    target_compile_options(ps2_ice_pico PRIVATE -Wformat=2 -Wno-format-nonliteral)
endif()

# Run everything from SRAM: an XIP cache miss in the middle of a sync scan
# costs microseconds, and the ROM puts a new byte on the bus every 216 ns.
pico_set_binary_type(ps2_ice_pico copy_to_ram)

target_link_libraries(ps2_ice_pico
    pico_stdlib
    hardware_pio
    hardware_gpio
    hardware_clocks
    hardware_vreg
)

if(ICE_DEBUG)
    target_compile_definitions(ps2_ice_pico PRIVATE ICE_DEBUG=1)
    target_link_libraries(ps2_ice_pico pico_multicore pico_util)
    # stdio over USB only, started by debug_log.c on core 0
    pico_enable_stdio_usb(ps2_ice_pico 1)
    pico_enable_stdio_uart(ps2_ice_pico 0)
    # never block on a missing terminal
    target_compile_definitions(ps2_ice_pico PRIVATE
        PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
        PICO_STDIO_DEFAULT_CRLF=1)
else()
    target_compile_definitions(ps2_ice_pico PRIVATE ICE_DEBUG=0)
    target_link_libraries(ps2_ice_pico pico_multicore)
    pico_enable_stdio_usb(ps2_ice_pico 0)
    pico_enable_stdio_uart(ps2_ice_pico 0)
endif()

pico_add_extra_outputs(ps2_ice_pico)
