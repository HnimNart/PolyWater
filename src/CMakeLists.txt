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

# 1. Change to INTERFACE. We don't need LIB_SOURCES or add_library(STATIC)
add_library(${LIB_NAME} INTERFACE)

# 2. INTERFACE libraries use the 'INTERFACE' keyword for definitions/includes
target_compile_definitions(${LIB_NAME} INTERFACE
    $<$<CONFIG:Debug>:ENABLE_PROFILING>
    $<$<CONFIG:RelWithDebInfo>:ENABLE_PROFILING>
)

# ... (Shader Compilation Logic remains the same) ...

# 3. Use an INTERFACE target to force shader generation
# Note: INTERFACE libraries cannot have PRIVATE target_sources or add_dependencies directly in older CMake.
# We attach the dependency to the INTERFACE target so anyone linking to 'common' triggers the shaders.
add_custom_target(generate_shaders DEPENDS ${SLANG_OUT} ${GLSL_OUT})
set_target_properties(generate_shaders PROPERTIES FOLDER "Shaders")

# 4. Attach generated headers and includes to the INTERFACE
target_include_directories(${LIB_NAME} INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${SHADER_OUTPUT_DIR}>
    ${ROOT_DIR}/ext/tinygltf
    ${ROOT_DIR}/ext/stb
)

# 5. Link sub-libraries as INTERFACE dependencies
target_link_libraries(${LIB_NAME} INTERFACE
    core_lib
    app_lib
    scene_lib
    generate_shaders # Triggers shader build when 'common' is used
)

# 6. Add subdirectories
add_subdirectory(core)
add_subdirectory(scene)
add_subdirectory(app)
add_subdirectory(backend)
add_subdirectory(renderer)

# --- 7. IDE Organization ---
# (source_group is mostly for visual organization in IDEs)
source_group("Shaders\\Slang" FILES ${SHADER_SLANG_FILES})
source_group("Shaders\\GLSL" FILES ${SHADER_GLSL_FILES})
source_group("Shaders\\Generated" FILES ${SLANG_OUT} ${GLSL_OUT})
