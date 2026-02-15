# -------------------------------------------------------------------
# Library: common
# -------------------------------------------------------------------
set(LIB_NAME common)

# --- Subdirectories & External Dependencies ---
find_package(nlohmann_json 3.10.5 REQUIRED)
find_package(Slang REQUIRED)
find_package(CUDAToolkit COMPONENTS CUDA::nvml QUIET)

add_library(${LIB_NAME} STATIC ${LIB_SOURCES})
file(GLOB LIB_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")
target_compile_features(${LIB_NAME} PUBLIC cxx_std_20)
set_target_properties(${LIB_NAME} PROPERTIES
    UNITY_BUILD OFF
    UNITY_BUILD_BATCH_SIZE 8
    UNITY_BUILD_MODE BATCH
)
target_compile_definitions(${LIB_NAME} PUBLIC
    $<$<CONFIG:Debug>:ENABLE_PROFILING>
    $<$<CONFIG:RelWithDebInfo>:ENABLE_PROFILING>
)

# --- 4. Shader Compilation Logic ---
include(CompilerGlslShader)
include(CompilerSlangShader)
file(GLOB_RECURSE SHADER_SLANG_FILES CONFIGURE_DEPENDS "shaders/*.slang")
# Filter out .h.slang headers from compilation list
list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")
file(GLOB_RECURSE SHADER_GLSL_FILES  CONFIGURE_DEPENDS "shaders/*.glsl")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/_autogen")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(SHADER_INCLUDE_FLAGS
    "-I${ROOT_DIR}"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders/shared"
    "-I${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
)




# Run custom compilation macros
compile_slang("${SHADER_SLANG_FILES}" "${SHADER_OUTPUT_DIR}" SLANG_OUT EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS})
compile_glsl("${SHADER_GLSL_FILES}" "${SHADER_OUTPUT_DIR}" GLSL_OUT EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS})

# Create dedicated target to force shader generation
add_custom_target(generate_shaders DEPENDS ${SLANG_OUT} ${GLSL_OUT})
set_source_files_properties(${SLANG_OUT} ${GLSL_OUT} PROPERTIES GENERATED TRUE)
add_dependencies(${LIB_NAME} generate_shaders)

# Attach shaders and generated headers to the library
target_sources(${LIB_NAME} PRIVATE 
    ${SHADER_SLANG_FILES} 
    ${SHADER_GLSL_FILES}
    ${SLANG_OUT} 
    ${GLSL_OUT}
)


add_subdirectory(core)
add_subdirectory(scene)
add_subdirectory(app)
add_subdirectory(backend)
add_subdirectory(renderer)


# --- Include Directories ---
target_include_directories(${LIB_NAME} PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${SHADER_OUTPUT_DIR}>
    ${ROOT_DIR}/ext/tinygltf
    ${ROOT_DIR}/ext/stb
)


# --- Linking ---
target_link_libraries(${LIB_NAME} PUBLIC
    core_lib
    app_lib
    scene_lib
)

# --- 7. IDE Organization ---
source_group("Sources" FILES ${LIB_SOURCES})
source_group("Shaders\\Slang" FILES ${SHADER_SLANG_FILES})
source_group("Shaders\\GLSL" FILES ${SHADER_GLSL_FILES})
source_group("Shaders\\Generated" FILES ${SLANG_OUT} ${GLSL_OUT})
