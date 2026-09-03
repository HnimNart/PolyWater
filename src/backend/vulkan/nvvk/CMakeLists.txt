set(LIB_NAME nvvk)

file(GLOB LIB_SOURCES "*.cpp" "*.hpp")
source_group("Source Files" FILES ${LIB_SOURCES})

add_library(${LIB_NAME} STATIC ${LIB_SOURCES})

if(MSVC)
  target_compile_options(${LIB_NAME} PRIVATE /W0)
else()
  target_compile_options(${LIB_NAME} PRIVATE -w)
endif()

get_filename_component(NVVK_PARENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
target_include_directories(${LIB_NAME} SYSTEM PUBLIC
    ${NVVK_PARENT_DIR}
    ${Slang_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${Vulkan_VOLK_DIR}
    ${Vulkan_VOLK_DIR}/..
    )


target_link_libraries(
  ${LIB_NAME}
  PUBLIC 
    core_lib
    glm 
    volk 
    vma 
    fmt 
    offsetAllocator
    Slang
)

target_precompile_headers(${LIB_NAME} PRIVATE
  <filesystem>
)

add_library(nvpro2::${LIB_NAME} ALIAS ${LIB_NAME})
set_property(TARGET ${LIB_NAME} PROPERTY FOLDER "nvpro_core2")

