cmake_minimum_required(VERSION 3.20)
project(can2vss-feeder VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(absl REQUIRED)
find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)
find_package(glog REQUIRED)
find_package(yaml-cpp REQUIRED)

# Try to find libvss-types first (shared dependency)
# This avoids it being included twice if both libvssdag and libkuksa-cpp try to fetch it
find_package(vss-types QUIET)
if(NOT vss-types_FOUND)
    # Check parent directory for development
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libvss-types/CMakeLists.txt")
        message(STATUS "Found libvss-types in parent directory")
        add_subdirectory(../libvss-types ${CMAKE_BINARY_DIR}/libvss-types)
    else()
        message(STATUS "libvss-types not found, will be fetched by dependencies")
    endif()
endif()

# Find or add libvssdag
find_package(vssdag QUIET)
if(NOT vssdag_FOUND)
    # Check parent directory for development
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libvssdag/CMakeLists.txt")
        message(STATUS "Using libvssdag from parent directory")
        set(BUILD_EXAMPLES OFF CACHE BOOL "Don't build vssdag examples" FORCE)
        set(BUILD_TESTS OFF CACHE BOOL "Don't build vssdag tests" FORCE)
        set(BUILD_INTEGRATION_TESTS OFF CACHE BOOL "Don't build vssdag integration tests" FORCE)
        add_subdirectory(../libvssdag ${CMAKE_BINARY_DIR}/libvssdag)
    else()
        message(FATAL_ERROR "libvssdag not found. Please install it or place it in parent directory.")
    endif()
endif()

# Find or add libkuksa-cpp
find_package(kuksa-cpp QUIET)
if(NOT kuksa-cpp_FOUND)
    # Check parent directory for development
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libkuksa-cpp/CMakeLists.txt")
        message(STATUS "Using libkuksa-cpp from parent directory")
        set(BUILD_EXAMPLES OFF CACHE BOOL "Don't build kuksa-cpp examples" FORCE)
        set(BUILD_TESTS OFF CACHE BOOL "Don't build kuksa-cpp tests" FORCE)
        add_subdirectory(../libkuksa-cpp ${CMAKE_BINARY_DIR}/libkuksa-cpp)
    else()
        message(FATAL_ERROR "libkuksa-cpp not found. Please install it or place it in parent directory.")
    endif()
endif()

# Main executable
add_executable(can2vss-feeder
    src/main.cpp
)

target_link_libraries(can2vss-feeder
    PRIVATE
        vssdag
        kuksa_cpp
        glog::glog
        yaml-cpp
        absl::status
        absl::statusor
)

# Install
install(TARGETS can2vss-feeder
    RUNTIME DESTINATION bin
)

# ============================================================================
# Tests
# ============================================================================
option(CAN2VSS_BUILD_TESTS "Build integration tests" ON)

if(CAN2VSS_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    add_executable(test_can2vss_feeder_integration
        tests/integration/test_can2vss_feeder_integration.cpp
    )

    target_link_libraries(test_can2vss_feeder_integration
        PRIVATE
            kuksa_cpp
            glog::glog
            GTest::gtest
            GTest::gtest_main
    )

    # Make sure can2vss-feeder is built before running the integration test
    add_dependencies(test_can2vss_feeder_integration can2vss-feeder)

    add_test(NAME can2vss_feeder_integration
        COMMAND test_can2vss_feeder_integration
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()
