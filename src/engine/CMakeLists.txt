# ---- Target ----
set(TARGET engine)

# Create the library (sources added by subdirectories)
add_library(${TARGET})

# Language + warnings
target_compile_features(${TARGET} PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(${TARGET} PRIVATE /W4 /permissive- -Wno-missing-field-initializers)
else()
  target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# Public includes (consumers get these)
target_include_directories(${TARGET}
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
)

# Dependencies (Vulkan etc.)
find_package(Vulkan REQUIRED)
target_link_libraries(${TARGET} PUBLIC Vulkan::Vulkan ${PLATFORM_LIBRARIES} nvpro_core)

# Public defines
target_compile_definitions(${TARGET}
  PUBLIC ENGINE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

# Nice to have for mixing static/shared
set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Add submodules that contribute sources to the target
add_subdirectory(backends)
add_subdirectory(tools)
add_subdirectory(shaders)
# (Optional) add more subdirs later:
# add_subdirectory(core)
# add_subdirectory(os)
# add_subdirectory(res)


# ----- COMPILE GLSL to SPIR-V -----
set(SPV_OUT_DIR ${CMAKE_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${SPV_OUT_DIR})
message(STATUS "___ ${CMAKE_CURRENT_SOURCE_DIR} - ${SPV_OUT_DIR}")
compile_glsl_directory(
	SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
	DST "${SPV_OUT_DIR}"
	VULKAN_TARGET "vulkan1.2"
	DEPENDENCY ${VULKAN_BUILD_DEPENDENCIES}
	)
