#===============================================================================
# PCA9685 driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   pca9685, consumed as pca9685::pca9685 both through add_subdirectory() and
#   through find_package(pca9685) after install.
#
#   cmake -S . -B build -DPCA9685_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/pca9685_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${PCA9685_SOURCE_FILES}
        INCLUDE_DIRS ${PCA9685_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${PCA9685_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(PCA9685_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(pca9685_driver
    VERSION ${PCA9685_VERSION}
    DESCRIPTION "C++20 driver for the NXP PCA9685 16-channel, 12-bit PWM controller on I2C."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because pca9685.hpp includes
# "../src/pca9685.ipp" relative to itself:
#   <prefix>/include/pca9685/inc/*.hpp, pca9685_version.h
#   <prefix>/include/pca9685/src/pca9685.ipp
set(PCA9685_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/pca9685/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${PCA9685_TARGET_NAME} INTERFACE)
target_include_directories(${PCA9685_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${PCA9685_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${PCA9685_INSTALL_INCLUDEDIR}>
)
target_compile_features(${PCA9685_TARGET_NAME} INTERFACE cxx_std_20)

# pca9685::pca9685 everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(pca9685::pca9685 ALIAS ${PCA9685_TARGET_NAME})
set_target_properties(${PCA9685_TARGET_NAME} PROPERTIES EXPORT_NAME pca9685)

option(PCA9685_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links pca9685::pca9685" OFF)
if(PCA9685_ENABLE_WARNINGS)
    target_compile_options(${PCA9685_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(PCA9685_BUILD_TESTS "Build the host tests in tests/" OFF)
option(PCA9685_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(PCA9685_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${PCA9685_TARGET_NAME} EXPORT pca9685Targets
    INCLUDES DESTINATION ${PCA9685_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${PCA9685_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${PCA9685_VERSION_HEADER}" DESTINATION ${PCA9685_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pca9685/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT pca9685Targets
    FILE pca9685Targets.cmake
    NAMESPACE pca9685::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pca9685)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/pca9685Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pca9685Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pca9685)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pca9685ConfigVersion.cmake"
    VERSION ${PCA9685_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pca9685Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pca9685ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pca9685)
