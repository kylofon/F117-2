cmake_minimum_required(VERSION 3.24)
project(f117port C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Faithful port of F-117A Nighthawk Stealth Fighter 2.0 (SETUP, MPS_LOGO, PLAYER, START, VGAME, END and
# DSWAP in one process, F117.COM's chain loop in src/prog.c), VGA mode 13h with AdLib sound
# (port/spec/platform.md). The game's data and executables are read from the game folder at run time.

find_package(SDL3 REQUIRED CONFIG)

# Nuked-OPL3 (LGPL-2.1), built as its own library and linked in; see third_party/nuked-opl3/README.md
# (the repository's, one level up).
add_library(nuked_opl3 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nuked-opl3/opl3.c)
target_include_directories(nuked_opl3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nuked-opl3)

# Coordinator files in src/, then one glob per module directory (PORTING.md "Source layout"): a module adds
# <module>/<file>.c and it is built without touching this file. programs/ holds the phase 4 placeholders.
set(F117_MODULES platform programs)
file(GLOB F117_SOURCES CONFIGURE_DEPENDS src/*.c)
foreach(m ${F117_MODULES})
    file(GLOB m_sources CONFIGURE_DEPENDS src/${m}/*.c)
    list(APPEND F117_SOURCES ${m_sources})
endforeach()

add_executable(f117 ${F117_SOURCES})
# src/gen holds the generated headers (symbols_<image>.h, module_<spec>.h; PORTING.md "Symbol pipeline"), committed
# and checked by the gen_*_current tests below. Both "x.h" and "gen/x.h" resolve.
target_include_directories(f117 PRIVATE src src/gen)
target_link_libraries(f117 PRIVATE SDL3::SDL3 nuked_opl3)
if(MSVC)
    target_compile_options(f117 PRIVATE /W3)
else()
    # The memory model reads multi-byte values through byte pointers (see src/mem.h).
    target_compile_options(f117 PRIVATE -Wall -Wextra -fno-strict-aliasing)
endif()
if(WIN32)
    # SDL3.dll, then what it loads in turn (MSYS2's SDL3.dll needs libiconv-2.dll), found where the compiler's
    # own DLLs are. Without it Windows takes whatever libiconv-2.dll is on the PATH; a 32-bit one fails with
    # 0xc000007b.
    get_filename_component(F117_DLL_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    add_custom_command(TARGET f117 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:f117> $<TARGET_FILE_DIR:f117>
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:f117> "-DSEARCH=${F117_DLL_DIR}"
                -DCMAKE_OBJDUMP=${CMAKE_OBJDUMP} -P ${CMAKE_CURRENT_SOURCE_DIR}/launcher/copy_dlls.cmake
        COMMAND_EXPAND_LISTS)
endif()

# Tests (ctest): the chain with the placeholder programs, the tick source, and the game-folder check.
enable_testing()
add_test(NAME chain COMMAND f117 --headless --chain-test)
set_tests_properties(chain PROPERTIES PASS_REGULAR_EXPRESSION "chain-test OK")
add_test(NAME tick_rate COMMAND f117 --headless --tick-test 1000)
# 70.09 Hz: 70 or 71 ticks in a second; a loaded machine may see a few fewer, never more than 72.
set_tests_properties(tick_rate PROPERTIES PASS_REGULAR_EXPRESSION "ticks in 1000 ms: (6[4-9]|7[0-2]) ")
add_test(NAME check_missing_files COMMAND f117 --check --game-dir ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(check_missing_files PROPERTIES WILL_FAIL TRUE)

# The generated headers are current: the generators re-run in memory and fail on any difference (tools/gen_*.py).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME gen_symbols_current COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_symbols.py --check)
    add_test(NAME gen_module_headers_current COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_module_headers.py --check)
else()
    message(WARNING "Python 3 not found: the gen_*_current tests are not registered")
endif()

# The launcher ("F-117A Nighthawk"): the game folder, the display and sound options; built beside
# f117.exe. It needs wxWidgets 3.2 (MSYS2: mingw-w64-x86_64-wxwidgets3.2-msw) and a C++17 compiler.
option(F117_LAUNCHER "Also build the launcher, F-117A Nighthawk.exe (wxWidgets)" OFF)
if(F117_LAUNCHER)
    add_subdirectory(launcher)
endif()
