cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(VulkanRendererTests CXX)
    set(TESTS_STANDALONE TRUE)
else()
    set(TESTS_STANDALONE FALSE)
endif()

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY git@github.com:google/googletest.git 
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE

)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

set_target_properties(gtest gtest_main PROPERTIES FOLDER "ThirdParty/GoogleTest")

enable_testing()

set(CORE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")

set(TEST_SOURCES
    test_math.cpp
    test_frustum.cpp
    test_bounding_box.cpp
    test_discrete_pdf.cpp
    test_bimap.cpp
    test_string_utils.cpp
)

if(TESTS_STANDALONE)
    list(APPEND TEST_SOURCES
        ${CORE_SRC_DIR}/math.cpp
        ${CORE_SRC_DIR}/frustum.cpp
        ${CORE_SRC_DIR}/bounding_box.cpp
        ${CORE_SRC_DIR}/discrete_pdf.cpp
    )
endif()

add_executable(unit_tests ${TEST_SOURCES})

target_compile_features(unit_tests PRIVATE cxx_std_20)

if(TESTS_STANDALONE)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glm)

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
            ROOT_DIR=\"/\"
            PROFILE_APP
    )
else()
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
