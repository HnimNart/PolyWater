# -------------------------------------------------------------------
# Library: common (Umbrella Library)
# -------------------------------------------------------------------
set(LIB_NAME common)

# --- Subdirectories & External Dependencies ---
find_package(nlohmann_json 3.10.5 REQUIRED)
find_package(Slang REQUIRED)
find_package(CUDAToolkit COMPONENTS CUDA::nvml QUIET)
include(DownloadZSTD)
include(DownloadZlibng)

# Umbrella target (no archive output)
add_library(${LIB_NAME} INTERFACE)

target_compile_definitions(${LIB_NAME} INTERFACE
    $<$<CONFIG:Debug>:ENABLE_PROFILING>
    $<$<CONFIG:RelWithDebInfo>:ENABLE_PROFILING>
)
#-- 4. Shader Compilation Logic ---
include(CompilerGlslShader)
include(CompilerSlangShader)

file(GLOB_RECURSE SHADER_SLANG_FILES CONFIGURE_DEPENDS "shaders/*.slang")
# Filter out .h.slang headers from compilation list
list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")

# On macOS, exclude the raytrace entrypoints (Metal raytracing often requires different handling)
if(APPLE)
    # This regex looks for 'entrypoints/raytrace' anywhere in the file path
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

# Determine backend + compile shaders
if(APPLE)
    set(ACTIVE_SHADER_BACKEND "metal")
    compile_slang_to_metal(
        "${SHADER_SLANG_FILES}"
        "${SHADER_OUTPUT_DIR}"
        EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
    )
else()
    set(ACTIVE_SHADER_BACKEND "vulkan")
    compile_slang_to_spirv(
        "${SHADER_SLANG_FILES}"
        "${SHADER_OUTPUT_DIR}"
        EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
    )
endif()

# GLSL compilation only for Vulkan builds
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

# Dedicated target to force shader generation
add_custom_target(generate_shaders DEPENDS ${SLANG_OUT} ${GLSL_OUT})
set_target_properties(generate_shaders PROPERTIES FOLDER "Shaders")
set_source_files_properties(${SLANG_OUT} ${GLSL_OUT} PROPERTIES GENERATED TRUE)

# -------------------------------------------------------------------
# Includes / Linking for the umbrella target
# -------------------------------------------------------------------
target_include_directories(${LIB_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${SHADER_OUTPUT_DIR}>
    ${ROOT_DIR}/ext/tinygltf
    ${ROOT_DIR}/ext/stb
)

# --- Linking ---
target_link_libraries(${LIB_NAME} INTERFACE
    core_lib
    app_lib
    scene_lib
)

# --- IDE Organization ---
source_group("Sources" FILES ${LIB_SOURCES})
# Ensure anything that links 'common' also builds shaders first
# (INTERFACE libs can carry "usage requirements", but not real build deps;
# so we attach shader build to sub-libs below after they exist.)
# We'll do that after add_subdirectory() calls.

# -------------------------------------------------------------------
# Subdirectories
# -------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(scene)
add_subdirectory(app)
add_subdirectory(backend)
add_subdirectory(renderer)

# Make the real libraries depend on shader generation (pick what makes sense)
add_dependencies(core_lib generate_shaders)
add_dependencies(scene_lib generate_shaders)
add_dependencies(app_lib generate_shaders)

# --- IDE Organization ---
source_group("Shaders\\Slang" FILES ${SHADER_SLANG_FILES})
source_group("Shaders\\GLSL" FILES ${SHADER_GLSL_FILES})
source_group("Shaders\\Generated" FILES ${SLANG_OUT} ${GLSL_OUT})
