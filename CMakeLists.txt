cmake_minimum_required(VERSION 3.16)
project(DrivOnix LANGUAGES CXX)

include(GNUInstallDirs)
option(DRIVONIX_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The graphical emulator currently supports Linux and Windows")
endif()

# Use native CSFML 2 libraries. The repository's sfml/*.a and *.dll files are
# legacy 32-bit Windows binaries and must never enter a Linux build.
set(CSFML_ROOT "" CACHE PATH "CSFML 2 installation prefix")
find_path(CSFML_INCLUDE_DIR SFML/Graphics.h HINTS ${CSFML_ROOT} ENV CSFML_ROOT PATH_SUFFIXES include)
foreach(component graphics window system)
    find_library(CSFML_${component}_LIBRARY NAMES csfml-${component}
        HINTS ${CSFML_ROOT} ENV CSFML_ROOT PATH_SUFFIXES lib lib64)
endforeach()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CSFML REQUIRED_VARS CSFML_INCLUDE_DIR
    CSFML_graphics_LIBRARY CSFML_window_LIBRARY CSFML_system_LIBRARY)
if(NOT CSFML_FOUND)
    message(FATAL_ERROR "Install the CSFML 2 development package or set CSFML_ROOT to its installation prefix")
endif()
file(STRINGS "${CSFML_INCLUDE_DIR}/SFML/Config.h" csfml_major REGEX "^#define CSFML_VERSION_MAJOR")
if(NOT csfml_major MATCHES "CSFML_VERSION_MAJOR +2$")
    message(FATAL_ERROR "DrivOnix requires CSFML 2.x (the CSFML 3 API is incompatible)")
endif()

add_library(drivonix_console STATIC
    Genesis/genesis.cpp Genesis/RomLoader/romLoader.cpp Genesis/GamePad/gamePad.cpp
    Bits/bitsUtils.cpp M68k/m68k.cpp YM7101/ym7101.cpp CathodeRayTube/crt.cpp
    Platform/platform.cpp)
target_compile_features(drivonix_console PUBLIC cxx_std_11)
set_target_properties(drivonix_console PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(drivonix_console SYSTEM PUBLIC ${CSFML_INCLUDE_DIR})
target_link_libraries(drivonix_console PUBLIC
    ${CSFML_graphics_LIBRARY} ${CSFML_window_LIBRARY} ${CSFML_system_LIBRARY})
target_compile_definitions(drivonix_console PRIVATE
    DRIVONIX_INSTALL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/drivonix")

if(WIN32)
    target_link_libraries(drivonix_console PUBLIC comdlg32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    target_link_libraries(drivonix_console PRIVATE PkgConfig::GTK3)
endif()
if(MSVC)
    target_compile_options(drivonix_console PRIVATE /utf-8 /bigobj)
endif()

add_executable(DrivOnix main.cpp)
target_link_libraries(DrivOnix PRIVATE drivonix_console)
if(DRIVONIX_SANITIZERS AND NOT MSVC)
    target_compile_options(drivonix_console PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(drivonix_console PUBLIC -fsanitize=address,undefined)
endif()
add_custom_command(TARGET DrivOnix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DrivOnix>/Bios" "$<TARGET_FILE_DIR:DrivOnix>/Config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Bios/DrivOnix.bios" "$<TARGET_FILE_DIR:DrivOnix>/Bios/DrivOnix.bios"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Config/config.txt" "$<TARGET_FILE_DIR:DrivOnix>/Config/config.txt"
    VERBATIM)

install(TARGETS DrivOnix RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES Bios/DrivOnix.bios DESTINATION ${CMAKE_INSTALL_DATADIR}/drivonix/Bios)
install(FILES Config/config.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/drivonix/Config)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME frontend_cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/UnitTests/frontend/test_cli.py --emulator $<TARGET_FILE:DrivOnix>)
    set_tests_properties(frontend_cli PROPERTIES TIMEOUT 30)
    add_executable(rom_loader_tests UnitTests/frontend/romLoaderTests.cpp)
    target_link_libraries(rom_loader_tests PRIVATE drivonix_console)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rom-loader-fixtures")
    add_test(NAME rom_loader COMMAND rom_loader_tests "${CMAKE_CURRENT_BINARY_DIR}/rom-loader-fixtures")
    set_tests_properties(rom_loader PROPERTIES TIMEOUT 30)
endif()
