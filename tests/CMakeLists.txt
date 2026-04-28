cmake_minimum_required(VERSION 3.22)

# Allow tests to be built as a standalone project or as part of the main build
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(VulkanRendererTests CXX)
    set(TESTS_STANDALONE TRUE)
else()
    set(TESTS_STANDALONE FALSE)
endif()

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
)

# Prevent GoogleTest from overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

set_target_properties(gtest gtest_main PROPERTIES FOLDER "ThirdParty/GoogleTest")

enable_testing()

set(CORE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")

# Collect all test source files
set(TEST_SOURCES
    test_math.cpp
    test_frustum.cpp
    test_bounding_box.cpp
    test_discrete_pdf.cpp
    test_bimap.cpp
    test_string_utils.cpp
)

# When building standalone, compile the required core sources directly
# (avoids pulling in the full Vulkan/Slang/GLFW build graph)
if(TESTS_STANDALONE)
    list(APPEND TEST_SOURCES
        ${CORE_SRC_DIR}/Math.cpp
        ${CORE_SRC_DIR}/Frustum.cpp
        ${CORE_SRC_DIR}/bounding_box.cpp
        ${CORE_SRC_DIR}/DiscretePdf.cpp
    )
endif()

add_executable(unit_tests ${TEST_SOURCES})

target_compile_features(unit_tests PRIVATE cxx_std_20)

if(TESTS_STANDALONE)
    # Fetch glm when standalone
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glm)

    # Fetch fmt when standalone
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG        11.1.4
        GIT_SHALLOW    TRUE
    )
    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(fmt)

    target_link_libraries(unit_tests
        PRIVATE
            gtest_main
            glm::glm
            fmt::fmt
    )

    target_include_directories(unit_tests
        PRIVATE
            ${CORE_SRC_DIR}
            ${CORE_SRC_DIR}/..
    )

    target_compile_definitions(unit_tests
        PRIVATE
            GLM_FORCE_RADIANS
            GLM_ENABLE_EXPERIMENTAL
            GLM_FORCE_XYZW_ONLY
            # Provide a dummy ROOT_DIR for logger/profiler if needed
            ROOT_DIR=\"/\"
            # Disable profiling macros that require Vulkan/NVTX
            PROFILE_APP
    )
else()
    # When part of the main build, reuse already-defined targets
    target_link_libraries(unit_tests
        PRIVATE
            gtest_main
            core_lib
            glm
    )

    target_include_directories(unit_tests
        PRIVATE
            ${CORE_SRC_DIR}
            ${CORE_SRC_DIR}/..
    )
endif()

include(GoogleTest)
gtest_discover_tests(unit_tests)
