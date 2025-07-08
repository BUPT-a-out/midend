cmake_minimum_required(VERSION 3.20)
project(midend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()


option(BUILD_TESTING "Build tests" OFF)

# Find packages
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Collect source files
file(GLOB_RECURSE IR_SOURCES "src/IR/*.cpp")
file(GLOB_RECURSE SUPPORT_SOURCES "src/Support/*.cpp")

# Create library
add_library(midend
    ${IR_SOURCES}
    ${TRANSFORM_SOURCES}
    ${SUPPORT_SOURCES}
)

target_include_directories(midend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(midend PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)


# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()


# Install rules
install(TARGETS midend
    EXPORT midendTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT midendTargets
    FILE midendTargets.cmake
    NAMESPACE midend::
    DESTINATION lib/cmake/midend
)

# Generate config files
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/midendConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/midendConfig.cmake"
    INSTALL_DESTINATION lib/cmake/midend
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/midendConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/midendConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/midendConfigVersion.cmake"
    DESTINATION lib/cmake/midend
)

# Print configuration summary
message(STATUS "midend Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  CXX Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build testing: ${BUILD_TESTING}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")