set(LIB_NAME common)

file(GLOB_RECURSE LIB_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

file(GLOB SHADERS_SOURCES shaders/*.*)

foreach(src IN LISTS LIB_SOURCES)
  message(STATUS "===> ${src}")
endforeach()
foreach(src IN LISTS SHADER_SOURCES)
  message(STATUS "===> ${src}")
endforeach()


# Adding the library here does not actually create a build target,
add_library(${LIB_NAME})
# Add the sources to the library target
target_sources(${LIB_NAME} PRIVATE ${LIB_SOURCES} ${SHADERS_SOURCES})

target_include_directories(${LIB_NAME} INTERFACE ${PROJECT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_autogen)

# Add the current directory to include path for the library
target_include_directories(${LIB_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR}/_autogen)

# Add dependencies
target_link_libraries(${LIB_NAME} PUBLIC
  glm
  volk # The Vulkan loader
  nvutils
  nvvk
  nvpro2::nvslang
)

# Make headers show up in IDE
source_group("Shaders" FILES ${SHADERS_SOURCES})

# Since this is actually header-only, we don't need to build anything
set_target_properties(${LIB_NAME} PROPERTIES LINKER_LANGUAGE CXX)


###################################
####### Compile shaders ###########
###################################
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/_autogen")
file(GLOB SHADER_GLSL_FILES "shaders/*.glsl")
file(GLOB SHADER_SLANG_FILES "shaders/*.slang")
list(FILTER SHADER_SLANG_FILES EXCLUDE REGEX ".*\\.h\\.slang$")

# Adding standard shaders (consistent across all samples)
list(APPEND SHADER_SLANG_FILES
    ${NVSHADERS_DIR}/nvshaders/sky_simple.slang
    ${NVSHADERS_DIR}/nvshaders/tonemapper.slang
)

# Build shader include flags
set(SHADER_INCLUDE_FLAGS "-I${NVSHADERS_DIR}" "-I${ROOT_DIR}" "-I${SHADER_DIR}" "-I${CMAKE_CURRENT_SOURCE_DIR}")

compile_slang(
    "${SHADER_SLANG_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

compile_glsl(
    "${SHADER_GLSL_FILES}"
    "${SHADER_OUTPUT_DIR}"
    GENERATED_SHADER_GLSL_HEADERS
    EXTRA_FLAGS ${SHADER_INCLUDE_FLAGS}
)

# Add shader files to the project
source_group("Shaders" FILES ${SHADER_SLANG_FILES} ${SHADER_GLSL_FILES} ${SHADER_H_FILES})
source_group("Shaders/Compiled" FILES ${GENERATED_SHADER_SLANG_HEADERS} ${GENERATED_SHADER_GLSL_HEADERS} ${GENERATED_SHADER_HEADERS})

# Add the output shader headers (target) directly to the executable
# This allow to compile the shaders when the executable is built
target_sources(${LIB_NAME} PUBLIC ${SHADER_SLANG_FILES} ${GENERATED_SHADER_SLANG_HEADERS} ${GENERATED_SHADER_GLSL_HEADERS} ${SHADER_H_FILES})