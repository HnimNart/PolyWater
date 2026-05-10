# This CMake file sets up targets for all third-party libraries included within
# this folder.
project(third_party LANGUAGES C CXX)

# We want static libraries for all packages defined here.
set(BUILD_SHARED_LIBS ON)

# Speed up CMake configuration by setting some properties if we recognize the toolchain.
include(SetKnownToolchainProperties.cmake)

# dxh
if(NOT TARGET dxh)
  set(_DXH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dxh)
  file(GLOB_RECURSE _DXH_FILES "${_DXH_DIR}/include/*")
  add_library(dxh INTERFACE ${_DXH_FILES})
  target_include_directories(dxh INTERFACE ${_DXH_DIR}/include)
  set_property(TARGET dxh PROPERTY FOLDER "ThirdParty")
endif()

# GLFW
if(NOT TARGET glfw)
  set(GLFW_USE_HYBRID_HPG ON)
  set(GLFW_VULKAN_STATIC OFF)
  set(GLFW_BUILD_DOCS OFF)
  set(GLFW_BUILD_EXAMPLES OFF)
  set(GLFW_BUILD_TESTS OFF)
  set(GLFW_BUILD_WAYLAND OFF)
  set(GLFW_INSTALL OFF)
  add_subdirectory(glfw)
  target_include_directories(glfw SYSTEM INTERFACE glfw/include)
# 3. Suppress warnings during the compilation of GLFW's .c files
  if(MSVC)
    target_compile_options(glfw PRIVATE /W0)
  else()
    target_compile_options(glfw PRIVATE -w)
  endif()
  set_target_properties(glfw update_mappings PROPERTIES FOLDER "ThirdParty/GLFW")
endif()


# volk (STATIC — built from SDK source)
#
# volk is compiled from the SDK's own volk.c and linked as a STATIC archive.
# Using find_package(volk CONFIG) is intentionally avoided: the SDK's
# volkConfig.cmake propagates a plain "-lvolk" linker flag rather than a full
# archive path, which fails unless libvolk.a happens to be in a standard
# system search directory.
#
# Building from source gives CMake the exact output path of the archive, so
# the full path (e.g. lib/Release/libvolk.a) is passed on every link line —
# no "-lvolk" flag, no search-path dependency.
#
# One copy of volk globals in the process is guaranteed as long as every
# library that consumes volk (nvvk, vk_backend, vk_render) is also STATIC:
# the linker folds them all into the final executable (or the outermost shared
# library) and produces exactly one instance of each PFN_vkXxx pointer.
# If any consumer becomes SHARED again, switch volk to SHARED as well.
find_package(VulkanExtra REQUIRED)

if(NOT Vulkan_VOLK_DIR)
  message(FATAL_ERROR
    "Vulkan SDK volk source directory not found (Vulkan_VOLK_DIR is not set). "
    "Make sure the Vulkan SDK is installed and the 'volk' component is present "
    "in the SDK's include directory.")
endif()

add_library(volk STATIC ${Vulkan_VOLK_DIR}/volk.c)
add_library(volk::volk ALIAS volk)

target_include_directories(volk PUBLIC ${Vulkan_VOLK_DIR} ${Vulkan_INCLUDE_DIRS})
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

if(WIN32)
  target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
elseif(APPLE)
  target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_METAL_EXT)
  target_link_libraries(volk PUBLIC ${CMAKE_DL_LIBS})
else()
  target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XCB_KHR VK_USE_PLATFORM_XLIB_KHR)
  target_link_libraries(volk PUBLIC ${CMAKE_DL_LIBS})
endif()

if(MSVC)
  target_compile_options(volk PRIVATE /W0)
else()
  target_compile_options(volk PRIVATE -w)
endif()
set_property(TARGET volk PROPERTY FOLDER "ThirdParty")

# imgui
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
if(NOT TARGET imgui)
    set(IMGUI_FILES
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_demo.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/imconfig.h"
        "${IMGUI_DIR}/imgui.h"
        "${IMGUI_DIR}/imgui_internal.h"
        "${IMGUI_DIR}/imstb_rectpack.h"
        "${IMGUI_DIR}/imstb_textedit.h"
        "${IMGUI_DIR}/imstb_truetype.h"
        # Backends
        "${IMGUI_DIR}/backends/imgui_impl_glfw.h"
        "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
        # imgui_impl_vulkan is intentionally NOT included here.
        # It must be compiled directly into the Vulkan rendering library (vk_backend)
        # so that it shares the same statically-linked volk copy.  Including it here
        # would cause volk's function-pointer globals to be duplicated across
        # libapp_lib.so and libvk_render.so, leaving vkCreateInstance NULL in one of
        # them and producing a SIGSEGV on the first Vulkan call.
        "${IMGUI_DIR}/backends/imgui_impl_opengl3_loader.h"
        "${IMGUI_DIR}/backends/imgui_impl_opengl3.h"
        "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    )

    add_library(imgui STATIC ${IMGUI_FILES})

    # 1. Silence warnings for ImGui's source files
    if(MSVC)
        target_compile_options(imgui PRIVATE /W0)
    else()
        target_compile_options(imgui PRIVATE -w)
    endif()

    # 2. Treat the include directories as SYSTEM
    target_include_directories(imgui SYSTEM PUBLIC 
        ${IMGUI_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Link against GLFW only; volk is NOT propagated through imgui because
    # imgui_impl_vulkan.cpp is compiled directly into vk_backend instead.
    target_link_libraries(imgui PUBLIC glfw)

    set_property(TARGET imgui PROPERTY FOLDER "ThirdParty")
endif()

# imgui Metal backend (Apple platforms only)
if(APPLE AND TARGET imgui)
    target_sources(imgui PRIVATE
        "${IMGUI_DIR}/backends/imgui_impl_metal.h"
        "${IMGUI_DIR}/backends/imgui_impl_metal.mm"
    )
    set_source_files_properties(
        "${IMGUI_DIR}/backends/imgui_impl_metal.mm"
        PROPERTIES LANGUAGE OBJCXX
    )
    target_link_libraries(imgui PUBLIC
        "-framework Metal"
        "-framework QuartzCore"
    )
endif()

# implot
if(NOT TARGET implot)
  set(IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/implot")
  set(IMPLOT_FILES
      "${IMPLOT_DIR}/implot.cpp" "${IMPLOT_DIR}/implot.h"
      "${IMPLOT_DIR}/implot_demo.cpp" "${IMPLOT_DIR}/implot_internal.h"
      "${IMPLOT_DIR}/implot_items.cpp"
  )

  add_library(implot STATIC ${IMPLOT_FILES})

  if(MSVC)
    target_compile_options(implot PRIVATE /W0)
  else()
    target_compile_options(implot PRIVATE -w)
  endif()

  target_include_directories(implot SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

  # -----------------------------------------------------------------------
  # FIX: Quotes are REQUIRED here because of the parentheses ()
  # -----------------------------------------------------------------------
  target_compile_definitions(implot PRIVATE
    IMPLOT_NO_FORCE_INLINE=1
    "IMPLOT_CUSTOM_NUMERIC_TYPES=(ImU32)(ImU64)(float)"
  )
  
  target_link_libraries(implot PRIVATE imgui)
  set_property(TARGET implot PROPERTY FOLDER "ThirdParty")
endif()


# glm
if(NOT TARGET glm)
  set(GLM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glm)
  add_library(glm INTERFACE ${GLM_DIR}/glm/glm.hpp)
  target_include_directories(glm INTERFACE ${GLM_DIR})
  target_compile_definitions(
    glm INTERFACE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL GLM_FORCE_XYZW_ONLY
  )
  set_property(TARGET glm PROPERTY FOLDER "ThirdParty")
endif()

# fmt
if(NOT TARGET fmt)
  set(FMT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fmt)
  if(EXISTS ${FMT_DIR}/CMakeLists.txt)
    add_subdirectory(${FMT_DIR})
    set_property(TARGET fmt PROPERTY FOLDER "ThirdParty")
  endif()
endif()

# stb
if(NOT TARGET stb)
  set(STB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stb)
  add_library(stb INTERFACE ${STB_DIR}/stb_image.h ${STB_DIR}/stb_image_write.h)
  target_include_directories(stb INTERFACE "${STB_DIR}")
  set_property(TARGET stb PROPERTY FOLDER "ThirdParty")
endif()

# tinygltf
if(NOT TARGET tinygltf)
  set(TINYGLTF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tinygltf)
  
  # INTERFACE libraries don't compile .cpp files, so we just manage the headers
  add_library(tinygltf INTERFACE)
  
  # 1. Use SYSTEM to tell the compiler to ignore warnings in these headers
  target_include_directories(tinygltf SYSTEM INTERFACE 
    ${TINYGLTF_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR} 
    ${STB_DIR}
  )

  # 2. Add definitions to reduce 'json.hpp' noise and disable exceptions if desired
  target_compile_definitions(tinygltf INTERFACE 
    TINYGLTF_NO_STB_IMAGE_WRITE  # Optional: skip writing if not needed
    JSON_NOEXCEPTION             # Reduces overhead and warnings in nlohmann/json
  )

  set_property(TARGET tinygltf PROPERTY FOLDER "ThirdParty")
endif()

#fmt
if(NOT TARGET fmt)
  add_library(fmt STATIC fmt/src/format.cc)
  target_include_directories(fmt PUBLIC fmt/include)
  set_property(TARGET fmt PROPERTY FOLDER "ThirdParty")
  # On MSVC, fmt requires either FMT_UNICODE to be defined as 0 or to enable
  # UTF-8 for source and execution character sets; see
  # https://github.com/fmtlib/fmt/commit/077e4ae746cde56559d72babe278e3d189bb2b3a.
  if(MSVC)
    target_compile_options(fmt PUBLIC $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
  endif()
endif()

# tinyobjloader
if(NOT TARGET tinyobjloader)
  set(TINYOBJLOADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader)
  add_library(tinyobjloader INTERFACE ${TINYOBJLOADER_DIR}/tiny_obj_loader.h)
  target_include_directories(
    tinyobjloader INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_property(TARGET tinyobjloader PROPERTY FOLDER "ThirdParty")
endif()


# vma
if(NOT TARGET vma)
  if(NOT Vulkan_VMA_DIR)
    message(FATAL_ERROR "Vulkan SDK VMA not found. Please install the Vulkan SDK into ext/vulkansdk (run scripts/install-vulkansdk.sh).")
  endif()
  add_library(vma INTERFACE)
  # The VulkanSDK places vk_mem_alloc.h in include/vma/; expose that directory
  # so consumers can #include <vk_mem_alloc.h> as before.
  target_include_directories(vma SYSTEM INTERFACE ${Vulkan_VMA_DIR})

  # vk_mem_alloc.h internally includes <vulkan/vulkan.h>.  Without
  # VK_NO_PROTOTYPES, Vulkan function declarations (VKAPI_CALL prototypes) are
  # emitted there, which then conflict with volk.h's extern PFN_xxx definitions
  # when both are included in the same translation unit.  Propagate
  # VK_NO_PROTOTYPES so every consumer of vk_mem_alloc.h skips the prototype
  # block, allowing volk to be the sole provider of Vulkan function symbols.
  # VMA_STATIC_VULKAN_FUNCTIONS=0 + VMA_DYNAMIC_VULKAN_FUNCTIONS=1 tells VMA to
  # load function pointers at runtime via volkLoadDevice / volkLoadInstance
  # rather than using statically-linked Vulkan loader functions.
  target_compile_definitions(vma INTERFACE
    VK_NO_PROTOTYPES
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
  )
  set_property(TARGET vma PROPERTY FOLDER "ThirdParty")
endif()



# thread_pool
if(NOT TARGET thread_pool)
  add_library(thread_pool INTERFACE)
  target_include_directories(
    thread_pool INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/thread_pool/include
  )
endif()

# offsetallocator
if(NOT TARGET offsetAllocator)
  set(OFFSETALLOCATOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/offsetallocator)
  
  add_library(offsetAllocator STATIC 
    ${OFFSETALLOCATOR_DIR}/offsetAllocator.cpp
    ${OFFSETALLOCATOR_DIR}/offsetAllocator.hpp
  )

  # 1. Silence warnings during compilation of the .cpp file
  if(MSVC)
    target_compile_options(offsetAllocator PRIVATE /W0)
  else()
    target_compile_options(offsetAllocator PRIVATE -w)
  endif()

  # 2. Treat the include directory as SYSTEM to silence header warnings in consumers
  target_include_directories(offsetAllocator SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

  set_property(TARGET offsetAllocator PROPERTY FOLDER "ThirdParty")
endif()

# KHR/GL headers
if(NOT TARGET gl_headers)
  add_library(gl_headers INTERFACE)
  target_include_directories(
    gl_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/gl_headers
  )
endif()



# Basis Universal
if(NOT TARGET basisu)
  set(_BASISU_DIR ${CMAKE_CURRENT_LIST_DIR}/basis_universal)
  file(GLOB_RECURSE _BASISU_FILES "${_BASISU_DIR}/transcoder/*.*" "${_BASISU_DIR}/encoder/*.*")
  
  add_library(basisu STATIC ${_BASISU_FILES})

  # -------------------------------------------------------------------
  # WARNING SUPPRESSION (NEW)
  # -------------------------------------------------------------------
  # 1. Silence warnings when compiling basisu's own source files
  if(MSVC)
    target_compile_options(basisu PRIVATE /W0)
  else()
    target_compile_options(basisu PRIVATE -w)
  endif()

  # 2. Treat basisu headers as SYSTEM for your project
  target_include_directories(basisu SYSTEM INTERFACE 
    "${_BASISU_DIR}/transcoder" 
    "${_BASISU_DIR}/encoder"
  )
  # -------------------------------------------------------------------

  # Keep your existing definitions...
  target_compile_definitions(basisu PRIVATE
    BASISD_SUPPORT_ATC=0
    BASISD_SUPPORT_DXT1=0
    BASISD_SUPPORT_ETC2_EAC_A8=0
    BASISD_SUPPORT_ETC2_EAC_RG11=0
    BASISD_SUPPORT_FXT1=0
    BASISD_SUPPORT_PVRTC1=0
    BASISD_SUPPORT_PVRTC2=0
  )

  # ... and your existing POSIX/SSE logic
  if(WIN32 AND NOT MSVC)
    add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE _CRT_INSECURE_NO_DEPRECATE)
  endif()

  if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "(x86_64|AMD64|i[3-6]86)")
    target_compile_definitions(basisu PRIVATE BASISU_SUPPORT_SSE=1)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      target_compile_options(basisu PRIVATE -msse4.2)
    endif()
  endif()

  target_link_libraries(basisu PUBLIC libzstd_static)
  set_property(TARGET basisu PROPERTY FOLDER "ThirdParty")
endif()


# cgltf
if (NOT TARGET cgltf AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)
  add_library(cgltf INTERFACE)
  target_include_directories(cgltf INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/cgltf")
  set_property(TARGET cgltf PROPERTY FOLDER "ThirdParty")
endif()

# NVTX
if (NOT TARGET nvtx3-cpp AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/NVTX/c/CMakeLists.txt)
  add_subdirectory(NVTX/c)
  # Set folder property for NVTX targets if they exist
  if(TARGET nvtx3-cpp)
    set_property(TARGET nvtx3-cpp PROPERTY FOLDER "ThirdParty")
  endif()
  if(TARGET nvtx3-c)
    set_property(TARGET nvtx3-c PROPERTY FOLDER "ThirdParty")
  endif()
endif()

# meshoptimizer
if (NOT TARGET meshoptimizer)
  set(MESHOPT_INSTALL OFF)
  set(MESHOPT_BUILD_DEMO OFF)
  set(MESHOPT_BUILD_SHARED_LIBS OFF)
  set(MESHOPT_STABLE_EXPORTS OFF)
  set(MESHOPT_WERROR OFF) # Important: stops meshopt from forcing its own strictness

  add_subdirectory(meshoptimizer)

  # 1. Silence warnings when compiling meshoptimizer.cpp
  if(MSVC)
    target_compile_options(meshoptimizer PRIVATE /W0)
  else()
    target_compile_options(meshoptimizer PRIVATE -w)
  endif()

  # 2. Treat the public headers (in src/) as SYSTEM to silence consumer warnings
  target_include_directories(meshoptimizer SYSTEM INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}/meshoptimizer/src"
  )

  set_property(TARGET meshoptimizer PROPERTY FOLDER "ThirdParty")
endif()

# BVH Library (Header-only)
if(NOT TARGET bvh)
    add_subdirectory(bvh)
    set_property(TARGET bvh PROPERTY FOLDER "ThirdParty")
endif()
