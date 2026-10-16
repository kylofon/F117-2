# The game's launcher, "F-117A Nighthawk": the game folder, the game speed, the display and sound
# options, then f117.
#
#   cmake -S launcher -B launcher/build -G Ninja && cmake --build launcher/build
#
# or, from the main project, -DF117_LAUNCHER=ON builds it next to f117.exe.
# Needs wxWidgets 3.2 (MSYS2: mingw-w64-x86_64-wxwidgets3.2-msw; Debian and Ubuntu: libwxgtk3.2-dev).
cmake_minimum_required(VERSION 3.24)
project(f117launcher LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

# MSYS2 and wxWidgets' own CMake builds install a package config; distributions
# such as Debian only ship wx-config, which CMake's FindwxWidgets uses.
find_package(wxWidgets 3.2 CONFIG QUIET COMPONENTS core base)
if(wxWidgets_FOUND AND TARGET wx::wxcore)
    set(F117L_WX_LIBRARIES wx::wxcore wx::wxbase)
else()
    find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base)
    include(${wxWidgets_USE_FILE})
    set(F117L_WX_LIBRARIES ${wxWidgets_LIBRARIES})
endif()

add_executable(f117launcher WIN32
    app.cpp
    game.cpp
    icon.cpp
    keys.cpp
    keysdialog.cpp
    launcher.cpp
    settings.cpp
)
set_target_properties(f117launcher PROPERTIES OUTPUT_NAME "F-117A Nighthawk" CXX_EXTENSIONS OFF)
target_compile_features(f117launcher PRIVATE cxx_std_17)
target_link_libraries(f117launcher PRIVATE ${F117L_WX_LIBRARIES})
# Built from the main project: beside f117.exe, where it looks for it.
if(NOT PROJECT_IS_TOP_LEVEL)
    set_target_properties(f117launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

if(WIN32)
    target_sources(f117launcher PRIVATE app.rc)
    target_link_libraries(f117launcher PRIVATE comctl32)
    if(MINGW)
        # The C++ runtime inside the exe, so the build needs fewer DLLs beside it.
        target_link_options(f117launcher PRIVATE -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
    # The wxWidgets DLLs and everything they load, found where the compiler's own DLLs are.
    get_filename_component(F117L_DLL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET f117launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:f117launcher> "-DSEARCH=${F117L_DLL_DIR}"
                -DCMAKE_OBJDUMP=${CMAKE_OBJDUMP} -P ${CMAKE_CURRENT_SOURCE_DIR}/copy_dlls.cmake)
endif()

if(MSVC)
    target_compile_options(f117launcher PRIVATE /W4 /utf-8)
else()
    target_compile_options(f117launcher PRIVATE -Wall -Wextra)
endif()
