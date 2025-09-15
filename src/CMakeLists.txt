cmake_minimum_required(VERSION 3.20)

# TODO seperate this into smaller

# ---- Project ----
project(engine LANGUAGES CXX)

# Build shared/static based on user preference
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# ---- Target name ----
set(TARGET engine)

# ---- Sources ----
file(GLOB_RECURSE SRC_VK
     CONFIGURE_DEPENDS
     engine/backends/vk/*.cpp
)

file(GLOB SRC_TOOLS
     CONFIGURE_DEPENDS
     engine/tools/assetc/importers/*.cpp
)

file(GLOB_RECURSE HEADERS
     CONFIGURE_DEPENDS
     engine/**/*.hpp
     engine/**/*.h
)

file(GLOB SHADERS
     CONFIGURE_DEPENDS
     engine/shaders/*.*    # collects .vert, .frag, .glsl, .rgen, etc.
)

# ---- Library ----
add_library(${TARGET})

target_sources(${TARGET}
    PRIVATE
        ${SRC_VK}
        ${SRC_TOOLS}
        ${HEADERS}
        ${SHADERS}
)

target_compile_features(${TARGET} PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(${TARGET} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(${TARGET}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/tools
)

find_package(Vulkan REQUIRED)
target_link_libraries(${TARGET} PUBLIC Vulkan::Vulkan ${PLATFORM_LIBRARIES} nvpro_core)

target_compile_definitions(${TARGET}
    PUBLIC
        ENGINE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders"
)

# ---- Source groups (IDE organization) ----
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/engine/backends/vk PREFIX "backends/vk" FILES ${SRC_VK})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/engine/tools/assetc/importers PREFIX "tools/assetc/importers" FILES ${SRC_TOOLS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/engine PREFIX "headers" FILES ${HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders PREFIX "shaders" FILES ${SHADERS})

set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ----- COMPILE GLSL to SPIR-V -----
set(SPV_OUT_DIR ${CMAKE_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${SPV_OUT_DIR})
compile_glsl_directory(
	SRC "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders"
	DST "${SPV_OUT_DIR}"
	VULKAN_TARGET "vulkan1.2"
	DEPENDENCY ${VULKAN_BUILD_DEPENDENCIES}
	)
