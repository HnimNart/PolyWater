# -------------------------------------------------------------------
# Library: common
# -------------------------------------------------------------------
set(LIB_NAME common)

# --- 1. Sources & Shaders ---
# Note: GLOB is convenient but requires re-running CMake when adding files.
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
file(GLOB_RECURSE SHADER_SLANG_FILES CONFIGURE_DEPENDS "shaders/*.slang")
file(GLOB_RECURSE SHADER_GLSL_FILES  CONFIGURE_DEPENDS "shaders/*.glsl")

# Filter out .h.slang files (headers) from compilation list
list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")

# --- 2. Define Target ---
add_library(${LIB_NAME} STATIC ${LIB_SOURCES})

# Enforce C++20 for this library specifically
target_compile_features(${LIB_NAME} PUBLIC cxx_std_20)

# --- 4. Dependencies ---
find_package(nlohmann_json 3.10.5 REQUIRED)
find_package(CUDAToolkit COMPONENTS CUDA::nvml QUIET)
find_package(Slang REQUIRED)
if(TARGET CUDA::nvml)
  target_link_libraries(${LIB_NAME} PUBLIC CUDA::nvml)
  target_compile_definitions(${LIB_NAME} PUBLIC NVML_SUPPORTED)
  message(STATUS "NVML: Supported")
else()
  message(STATUS "CUDA (NVML) Toolkit not found.")
endif()

target_link_libraries(${LIB_NAME}
    PUBLIC
        glm
        volk
        glfw
        vma
        imgui
        implot
        fmt
        nlohmann_json::nlohmann_json
        Slang
)

# --- 3. Include Directories ---
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
        
        # External includes
        "${PROJECT_SOURCE_DIR}/ext/nvpro_core2/third_party/tinygltf"
        "${PROJECT_SOURCE_DIR}/ext/nvpro_core2/third_party/stb"
        ${Slang_INCLUDE_DIR}
        
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/backend/vulkan"
        "${CMAKE_CURRENT_SOURCE_DIR}/backend/vulkan/nvvk"
)



# Optional CUDA NVML support
if(TARGET CUDA::nvml)
    target_link_libraries(${LIB_NAME} PUBLIC CUDA::nvml)
    target_compile_definitions(${LIB_NAME} PUBLIC NVML_SUPPORTED)
    message(STATUS "[${LIB_NAME}] NVML Support: ENABLED")
else()
    message(STATUS "[${LIB_NAME}] NVML Support: DISABLED (Not found)")
endif()

# Compiler Definitions
target_compile_definitions(${LIB_NAME} PRIVATE
    $<$<CONFIG:Debug>:ENABLE_PROFILING>
    $<$<CONFIG:RelWithDebInfo>:ENABLE_PROFILING>
)

# --- 5. Shader Compilation ---
# Use the common variable from root if available, else default to binary dir
if(NOT DEFINED SHADER_OUTPUT_DIR)
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/_autogen")
endif()

set(SHADER_INCLUDE_FLAGS
    "-I${NVSHADERS_DIR}"
    "-I${ROOT_DIR}"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "-I${CMAKE_CURRENT_SOURCE_DIR}"
)

# Compile Slang Shaders
compile_slang(
    "${SHADER_SLANG_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_SLANG_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

# Compile GLSL Shaders
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

# --- 6. IDE Organization (Visual Studio filters) ---
source_group("Sources" FILES ${LIB_SOURCES})
source_group("Shaders\\Slang" FILES ${SHADER_SLANG_FILES})
source_group("Shaders\\GLSL" FILES ${SHADER_GLSL_FILES})
source_group("Shaders\\Generated" FILES 
    ${GENERATED_SHADER_SLANG_HEADERS} 
    ${GENERATED_SHADER_GLSL_HEADERS}
)

