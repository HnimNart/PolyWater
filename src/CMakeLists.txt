# -------------------------------------------------------------------
# Library: common
# -------------------------------------------------------------------
set(LIB_NAME common)

# --- 1. Sources & Shaders ---
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
file(GLOB_RECURSE SHADER_SLANG_FILES CONFIGURE_DEPENDS "shaders/*.slang")
file(GLOB_RECURSE SHADER_GLSL_FILES  CONFIGURE_DEPENDS "shaders/*.glsl")

# Filter out .h.slang files (headers) from compilation list
list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")

# --- 2. Define Target ---
add_library(${LIB_NAME} STATIC ${LIB_SOURCES})

# Enforce C++20 for this library specifically
target_compile_features(${LIB_NAME} PUBLIC cxx_std_20)

# --- Enable Unity Build for this target only ---
set_target_properties(${LIB_NAME} PROPERTIES
    UNITY_BUILD OFF
    UNITY_BUILD_BATCH_SIZE 8
    UNITY_BUILD_MODE BATCH
)

# Compiler Definitions (Profiling only in Debug/RelWithDebInfo)
target_compile_definitions(${LIB_NAME} PUBLIC
    $<$<CONFIG:Debug>:ENABLE_PROFILING>
    $<$<CONFIG:RelWithDebInfo>:ENABLE_PROFILING>
    ROOT_DIR=\"${CMAKE_SOURCE_DIR}/\"
)


add_subdirectory(core)

# --- 3. Dependencies ---
find_package(nlohmann_json 3.10.5 REQUIRED)
find_package(Slang REQUIRED)
find_package(CUDAToolkit COMPONENTS CUDA::nvml QUIET)

target_link_libraries(${LIB_NAME}
    PUBLIC
        core_lib
        glm
        volk
        glfw
        vma
        imgui
        implot
        fmt
        nlohmann_json::nlohmann_json
        Slang
        offsetAllocator
)

# Optional CUDA NVML support (Merged logic)
if(TARGET CUDA::nvml)
    target_link_libraries(${LIB_NAME} PUBLIC CUDA::nvml)
    target_compile_definitions(${LIB_NAME} PUBLIC NVML_SUPPORTED)
    message(STATUS "[${LIB_NAME}] NVML Support: ENABLED")
else()
    message(STATUS "[${LIB_NAME}] NVML Support: DISABLED (Not found)")
endif()

# --- 4. Include Directories ---
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
        
        # External header-only libs (if not targets)
        "${ROOT_DIR}/ext/tinygltf"
        "${ROOT_DIR}/ext/stb"
        ${Slang_INCLUDE_DIR}
        
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/backend/vulkan"
        "${CMAKE_CURRENT_SOURCE_DIR}/backend/vulkan/nvvk"
)

# --- 5. Shader Compilation ---
if(NOT DEFINED SHADER_OUTPUT_DIR)
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/_autogen")
endif()

set(SHADER_INCLUDE_FLAGS
    "-I${NVSHADERS_DIR}"
    "-I${ROOT_DIR}"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "-I${CMAKE_CURRENT_SOURCE_DIR}"
)

# Compile Slang
compile_slang(
    "${SHADER_SLANG_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_SLANG_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

# Compile GLSL
compile_glsl(
    "${SHADER_GLSL_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_GLSL_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

# Add Generated Shaders to Target
target_sources(${LIB_NAME} PRIVATE
    ${SHADER_SLANG_FILES}
    ${SHADER_GLSL_FILES}
    ${GENERATED_SHADER_SLANG_HEADERS}
    ${GENERATED_SHADER_GLSL_HEADERS}
)

# --- 6. IDE Organization ---
source_group("Sources" FILES ${LIB_SOURCES})
source_group("Shaders\\Slang" FILES ${SHADER_SLANG_FILES})
source_group("Shaders\\GLSL" FILES ${SHADER_GLSL_FILES})
source_group("Shaders\\Generated" FILES 
    ${GENERATED_SHADER_SLANG_HEADERS} 
    ${GENERATED_SHADER_GLSL_HEADERS}
)

# --- Proxy Target for IDE/LSP Support ---
# This target is never built, but it forces CMake to generate 
# compile commands for the ORIGINAL files instead of the Unity blobs.
add_library(${LIB_NAME}_ide INTERFACE)

# Add all original sources to this interface target
target_sources(${LIB_NAME}_ide INTERFACE ${LIB_SOURCES})

# Inherit all include paths and definitions from the main library
target_link_libraries(${LIB_NAME}_ide INTERFACE ${LIB_NAME})

# Force CMake to treat these as "compiled" in the JSON database
set_target_properties(${LIB_NAME}_ide PROPERTIES 
    EXPORT_COMPILE_COMMANDS ON
    UNITY_BUILD OFF
)
