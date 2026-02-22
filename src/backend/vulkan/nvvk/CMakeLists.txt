# NVVK contains Vulkan helpers.
set(LIB_NAME nvvk)

# Collect all source files in the utils directory
file(GLOB LIB_SOURCES "*.cpp" "*.hpp")
source_group("Source Files" FILES ${LIB_SOURCES})

# Define the utils library
add_library(${LIB_NAME} ${LIB_SOURCES})

# -------------------------------------------------------------------
# WARNING SUPPRESSION (NEW)
# -------------------------------------------------------------------
# 1. Suppress warnings when compiling these specific .cpp files
if(MSVC)
  target_compile_options(${LIB_NAME} PRIVATE /W0)
else()
  target_compile_options(${LIB_NAME} PRIVATE -w)
endif()

# -------------------------------------------------------------------
# INCLUDES & LINKING
# -------------------------------------------------------------------
# All libraries use nvpro_core2 as their include root
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH PARENT_DIR)

# 2. Treat these includes as SYSTEM (ignores warnings in headers)
target_include_directories(${LIB_NAME} SYSTEM PUBLIC ${PARENT_DIR} ${Slang_INCLUDE_DIR})

target_link_libraries(
  ${LIB_NAME}
  PUBLIC 
         core_lib
         glm # Math library
         volk # The Vulkan loader
         vma # Vulkan Memory Allocator
         fmt # Formatting library
         offsetAllocator # OffsetAllocator library
         Slang
)

target_precompile_headers(${LIB_NAME} PRIVATE
  <filesystem>
)

add_library(nvpro2::${LIB_NAME} ALIAS ${LIB_NAME})
set_property(TARGET ${LIB_NAME} PROPERTY FOLDER "nvpro_core2")

