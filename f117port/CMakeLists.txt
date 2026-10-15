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
set(F117_MODULES platform video frontend shell sim render3d programs)
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
# The platform layer (src/platform, S5.1): RTL against vectors computed from the original under Unicorn
# (port/notes/s51/platform_emu.py), DOS files in a scratch folder, keyboard scan codes, joystick scaling, timer; and the
# loader on the real programs in ../Game (skipped, code 77, when the game files are not there).
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/platform_test_dir)
add_test(NAME platform COMMAND f117 --headless --platform-test --game-dir ${CMAKE_CURRENT_BINARY_DIR}/platform_test_dir)
set_tests_properties(platform PROPERTIES PASS_REGULAR_EXPRESSION "platform-test OK")
add_test(NAME exe_load COMMAND f117 --headless --exe-test --game-dir ${CMAKE_CURRENT_SOURCE_DIR}/../Game)
set_tests_properties(exe_load PROPERTIES SKIP_RETURN_CODE 77)

# The video module (src/video, S5.2): the driver primitives bit-exact against the ORIGINAL MGRAPHIC under Unicorn (vectors from
# port/notes/s52/video_emu.py; text scenarios are skipped without ../Game/MGRAPHIC.EXE), every PIC / PAN / PAL / font of ../Game
# against the Phase 2 Python decoders (port/notes/s52/decoder_ref.py), and the composed screen (--video-test writes PNGs to
# work/s52/screen for the visual check U-video). The two that need the game files skip with code 77 without them.
add_test(NAME video_selftest COMMAND f117 --headless --video-selftest --game-dir ${CMAKE_CURRENT_SOURCE_DIR}/../Game)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../work/s52/screen)
add_test(NAME video_screen COMMAND f117 --headless --video-test --game-dir ${CMAKE_CURRENT_SOURCE_DIR}/../Game)
set_tests_properties(video_screen PROPERTIES SKIP_RETURN_CODE 77
                     ENVIRONMENT "F117_SNAPSHOT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../work/s52/screen")
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME video_decoders COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../port/notes/s52/decoder_ref.py
             $<TARGET_FILE:f117> ${CMAKE_CURRENT_SOURCE_DIR}/../Game ${CMAKE_CURRENT_BINARY_DIR}/decoder_ref)
    set_tests_properties(video_decoders PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The front end (src/frontend, S5.3): SETUP, MPS_LOGO and PLAYER against the ORIGINAL programs run under Unicorn (port/notes/s53/frontend_ref.py:
# the text screens and the comm block of SETUP, the screen + DAC CRC of each of the 82 logo frames) and the Phase 2 PAN decoder (every cel of every
# .PAN through PLAYER's ring buffer), the fade formula, the 60.08 Hz animation clock and the logo pacing; `frontend_chain` is the scripted run
# (F117_KEYS answers SETUP, keys end the PLAYER runs) through the chain up to the START placeholder. Both skip (77) without ../Game.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME frontend COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../port/notes/s53/frontend_ref.py
             $<TARGET_FILE:f117> ${CMAKE_CURRENT_SOURCE_DIR}/../Game ${CMAKE_CURRENT_BINARY_DIR}/frontend_ref)
    add_test(NAME frontend_chain COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../port/notes/s53/frontend_ref.py --chain
             $<TARGET_FILE:f117> ${CMAKE_CURRENT_SOURCE_DIR}/../Game)
    set_tests_properties(frontend frontend_chain PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 150)
endif()

# The shell (src/shell, S5.4): the ORIGINAL START.EXE under Unicorn (port/notes/s54/shell_emu.py) runs two scripts (pilot creation, options, orders,
# briefing map, orders pages, arming, launch; a roster read and saved again); the port runs them on a scratch copy of the game folder and must give
# the same screen + DAC CRC at every step, the same comm block (the generated mission, byte for byte) and the same ROSTER.FIL. `shell_chain` is the
# scripted run of the whole chain (F117_KEYS for SETUP and the intro, F117_SHELL_SCRIPT for START): new pilot, strike mission, launch, VGAME and
# END placeholders, START again, quit. Both skip (77) without ../Game.
if(Python3_Interpreter_FOUND)
    add_test(NAME shell COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../port/notes/s54/shell_ref.py
             $<TARGET_FILE:f117> ${CMAKE_CURRENT_SOURCE_DIR}/../Game ${CMAKE_CURRENT_BINARY_DIR}/shell_ref)
    add_test(NAME shell_chain COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../port/notes/s54/shell_ref.py --chain
             $<TARGET_FILE:f117> ${CMAKE_CURRENT_SOURCE_DIR}/../Game)
    set_tests_properties(shell shell_chain PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
endif()

# The sim flow (src/sim, S5.5): VGAME's start-up state, the ISR body and 1262 function-call scenarios against the ORIGINAL VGAME.EXE under Unicorn
# (port/notes/s55/sim_ref.py: the whole DGROUP, the three pages row by row, the DAC and the comm block after every scenario; render3d, flight, weapons,
# AI and damage are stubs on both sides). It needs the comm block of the `shell` test (or F117_COMM). `shell_chain` also runs the real VGAME (a few
# frames, F117_VGAME_FRAMES). Skips (77) without ../Game.
if(Python3_Interpreter_FOUND)
    add_test(NAME sim_vgame COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../port/notes/s55/sim_ref.py
             $<TARGET_FILE:f117> ${CMAKE_CURRENT_SOURCE_DIR}/../Game ${CMAKE_CURRENT_BINARY_DIR}/sim_ref)
    set_tests_properties(sim_vgame PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800 DEPENDS shell)
endif()

# The render3d leaf routines (src/render3d, S5.6a): the 12EF clipper, the 1359 raster back end, segment 1001's hz_draw, the 1442 sky/ground bands and the 1433 maths
# against the Python references of port/notes/s37 (R7b_ref.py, R7c_ref.py; 0 mismatches against the original under Unicorn) on thousands of random cases per routine
# (port/notes/s56/r3d_leaf_ref.py writes the vectors, `f117 --r3d-test` runs them). Needs work/unpacked/VGAME.EXE for the tables; skips (77) without it.
if(Python3_Interpreter_FOUND)
    add_test(NAME render3d_leaf COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/../port/notes/s56/r3d_leaf_ref.py
             $<TARGET_FILE:f117> ${CMAKE_CURRENT_BINARY_DIR}/r3d_leaf)
    set_tests_properties(render3d_leaf PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900 PASS_REGULAR_EXPRESSION "r3d-test OK")
endif()

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
