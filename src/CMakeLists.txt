set(LIB_NAME common)

###################################
# Sources
###################################
file(GLOB_RECURSE LIB_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

message(STATUS "====> LIB_SOURCES:")
foreach(src IN LISTS LIB_SOURCES)
    message(STATUS "  ${src}")
endforeach()

file(GLOB SHADER_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.*"
)

message(STATUS "====> SHADER_SOURCES:")
foreach(src IN LISTS SHADER_SOURCES)
    message(STATUS "  ${src}")
endforeach()

###################################
# Library target
###################################
add_library(${LIB_NAME} SHARED)

target_sources(${LIB_NAME}
    PRIVATE
        ${LIB_SOURCES}
        ${SHADER_SOURCES}
)

set_target_properties(${LIB_NAME} PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 8
)

###################################
# Include paths
###################################
target_include_directories(${LIB_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/_autogen
)

###################################
# Dependencies
###################################
target_link_libraries(${LIB_NAME}
    PUBLIC
        glm
        volk
        nvutils
        nvvk
        nvpro2::nvslang
)

###################################
# IDE organization
###################################
source_group("Shaders" FILES ${SHADER_SOURCES})

###################################
#### Shader compilation
###################################
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/_autogen")

file(GLOB SHADER_GLSL_FILES CONFIGURE_DEPENDS "${SHADER_DIR}/*.glsl")
file(GLOB SHADER_SLANG_FILES CONFIGURE_DEPENDS "${SHADER_DIR}/*.slang")
list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")

# Shared standard shaders
list(APPEND SHADER_SLANG_FILES
    ${NVSHADERS_DIR}/nvshaders/sky_simple.slang
    ${NVSHADERS_DIR}/nvshaders/tonemapper.slang
)

set(SHADER_INCLUDE_FLAGS
    "-I${NVSHADERS_DIR}"
    "-I${ROOT_DIR}"
    "-I${SHADER_DIR}"
    "-I${CMAKE_CURRENT_SOURCE_DIR}"
)

compile_slang(
    "${SHADER_SLANG_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_SLANG_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

compile_glsl(
    "${SHADER_GLSL_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_GLSL_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

###################################
# Attach generated shaders to lib
###################################
target_sources(${LIB_NAME}
    PRIVATE
        ${SHADER_SLANG_FILES}
        ${SHADER_GLSL_FILES}
        ${GENERATED_SHADER_SLANG_HEADERS}
        ${GENERATED_SHADER_GLSL_HEADERS}
)

source_group("Shaders/Compiled"
    FILES
        ${GENERATED_SHADER_SLANG_HEADERS}
        ${GENERATED_SHADER_GLSL_HEADERS}
)
