#===============================================================================
# PCA9685 driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(PCA9685_TARGET_NAME "pca9685")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(PCA9685_VERSION_MAJOR 0)
set(PCA9685_VERSION_MINOR 1)
set(PCA9685_VERSION_PATCH 0)
set(PCA9685_VERSION "${PCA9685_VERSION_MAJOR}.${PCA9685_VERSION_MINOR}.${PCA9685_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(PCA9685_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/pca9685_version.h.in")
set(PCA9685_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/pca9685_generated")
set(PCA9685_VERSION_HEADER "${PCA9685_VERSION_HEADER_DIR}/pca9685_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${PCA9685_VERSION_HEADER_DIR}")
configure_file("${PCA9685_VERSION_TEMPLATE}" "${PCA9685_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(PCA9685_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${PCA9685_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/pca9685.ipp, included by inc/pca9685.hpp.
set(PCA9685_SOURCE_FILES)

set(PCA9685_IDF_REQUIRES)
