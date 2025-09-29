cmake_minimum_required(VERSION 3.9.6 FATAL_ERROR)

# Prefer clang
if(NOT CMAKE_CXX_COMPILER)
  find_program(CLANGXX clang++)
  if(CLANGXX)
    message(STATUS "Using clang++: ${CLANGXX}")
    set(CMAKE_CXX_COMPILER ${CLANGXX} CACHE STRING "" FORCE)
  endif()
endif()

if(NOT CMAKE_C_COMPILER)
  find_program(CLANGC clang)
  if(CLANGC)
    message(STATUS "Using clang: ${CLANGC}")
    set(CMAKE_C_COMPILER ${CLANGC} CACHE STRING "" FORCE)
  endif()
endif()

# Force compiler diagnostics to be colored
set(CMAKE_COLOR_DIAGNOSTICS ON)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif (MSVC)
    add_compile_options(/diagnostics:column /FC)
endif()

project(vulkan_engine)

#--------------------------------------------------------------------------------------------------
# look for nvpro_core 1) as a sub-folder 2) at some other locations
# this cannot be put anywhere else since we still didn't find setup.cmake yet
if(NOT LIB_DIRECTORY)

  find_path(LIB_DIRECTORY
    NAMES nvpro_core/cmake/setup.cmake
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ext
    REQUIRED
    DOC "Directory containing nvpro_core"
    )
endif()


set(BUILD_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

## Various functions and macros REQUIRED
if(EXISTS ${LIB_DIRECTORY}/nvpro_core/cmake/setup.cmake)
  include(${LIB_DIRECTORY}/nvpro_core/cmake/setup.cmake)
  include(${LIB_DIRECTORY}/nvpro_core/cmake/utilities.cmake)
else()
  message(FATAL_ERROR "could not find base directory, please set LIB_DIRECTORY to folder containing nvpro_core")
endif()

if(MSVC)
    add_definitions(/wd26812)  # 'enum class' over 'enum'
    add_definitions(/wd26451)  # Arithmetic overflow, casting 4 byte value to 8 byte value
endif()

#--------------------------------------------------------------------------------------------------
# Package shared by all projects
_add_package_VulkanSDK()
_add_package_ImGUI()
_add_nvpro_core_lib()

message(STATUS "COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets to  ${OUTPUT_PATH}")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${OUTPUT_PATH})


#--------------------------------------------------------------------------------------------------
# Compile engine
add_subdirectory(src)  # path to your repo root

#--------------------------------------------------------------------------------------------------
# Compile applications
add_subdirectory(app)

#--------------------------------------------------------------------------------------------------
# Install - copying the media directory
install(DIRECTORY "assets"
        CONFIGURATIONS Release
        DESTINATION "bin_${ARCH}")
install(DIRECTORY "assets"
        CONFIGURATIONS Debug
        DESTINATION "bin_${ARCH}_debug")


#--------------------------------------------------------------------------------------------------
# Custom targets
file(GLOB_RECURSE ALL_SRC
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.hpp
)
add_custom_target(format
    COMMAND echo ${ALL_SRC}
    COMMAND clang-format -i ${ALL_SRC}
    COMMENT "Formatting all source files"
)

add_custom_target(tidy
    COMMAND run-clang-tidy -p ${CMAKE_BINARY_DIR}
    COMMENT "Running clang-tidy analysis ${CMAKE_BINARY_DIR}"
)
