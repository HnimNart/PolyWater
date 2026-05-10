set(LIB_NAME common)

find_package(nlohmann_json 3.10.5 REQUIRED)
find_package(Slang REQUIRED)
find_package(CUDAToolkit COMPONENTS CUDA::nvml QUIET)
include(DownloadZSTD)
include(DownloadZlibng)

add_library(${LIB_NAME} INTERFACE)

target_compile_definitions(${LIB_NAME} INTERFACE
    $<$<CONFIG:Debug>:ENABLE_PROFILING>
    $<$<CONFIG:RelWithDebInfo>:ENABLE_PROFILING>
)
target_compile_definitions(${LIB_NAME} INTERFACE
    $<$<CONFIG:Debug>:PROFILE_APP>
    $<$<CONFIG:RelWithDebInfo>:PROFILE_APP>
)

include(CompilerGlslShader)
include(CompilerSlangShader)

file(GLOB_RECURSE SHADER_SLANG_FILES CONFIGURE_DEPENDS "shaders/*.slang")
list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")

if(APPLE)
    list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX "entrypoints/raytrace/.*")
    message(STATUS "macOS detected: Excluding raytrace shaders from auto-compilation")
endif()

file(GLOB_RECURSE SHADER_GLSL_FILES CONFIGURE_DEPENDS "shaders/*.glsl")

set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/_autogen")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
set(SHADER_INCLUDE_FLAGS
    "-I${ROOT_DIR}"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders/shared"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
)

if(APPLE)
    set(ACTIVE_SHADER_BACKEND "metal")
    compile_slang_to_metal(
        "${SHADER_SLANG_FILES}"
        "${SHADER_OUTPUT_DIR}"
        SLANG_OUT
        EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
    )
else()
    set(ACTIVE_SHADER_BACKEND "vulkan")
    compile_slang_to_spirv(
        "${SHADER_SLANG_FILES}"
        "${SHADER_OUTPUT_DIR}"
        SLANG_OUT
        EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
    )
endif()

if(ACTIVE_SHADER_BACKEND STREQUAL "vulkan")
    compile_glsl(
        "${SHADER_GLSL_FILES}"
        "${SHADER_OUTPUT_DIR}"
        GLSL_OUT
        EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
    )
else()
    set(GLSL_OUT "")
    if(SHADER_GLSL_FILES)
        message(WARNING
            "GLSL shaders found, but the Metal backend is active. "
            "These will not be compiled. Consider porting them to Slang."
        )
    endif()
endif()

add_custom_target(generate_shaders DEPENDS ${SLANG_OUT} ${GLSL_OUT})
set_target_properties(generate_shaders PROPERTIES FOLDER "Shaders")
set_source_files_properties(${SLANG_OUT} ${GLSL_OUT} PROPERTIES GENERATED TRUE)

target_include_directories(${LIB_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${SHADER_OUTPUT_DIR}>
    ${ROOT_DIR}/ext/tinygltf
    ${ROOT_DIR}/ext/stb
)

target_link_libraries(${LIB_NAME} INTERFACE
    core_lib
    app_lib
    scene_lib
)

source_group("Sources" FILES ${LIB_SOURCES})

add_subdirectory(core)
add_subdirectory(scene)
add_subdirectory(app)
add_subdirectory(backend)
add_subdirectory(renderer)

add_dependencies(core_lib generate_shaders)
add_dependencies(scene_lib generate_shaders)
add_dependencies(app_lib generate_shaders)

source_group("Shaders\\Slang" FILES ${SHADER_SLANG_FILES})
source_group("Shaders\\GLSL" FILES ${SHADER_GLSL_FILES})
source_group("Shaders\\Generated" FILES ${SLANG_OUT} ${GLSL_OUT})
