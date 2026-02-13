cmake_minimum_required(VERSION 3.22...3.31)
project(my_render VERSION 1.0 LANGUAGES C CXX)

# --- 1. Global Compiler Settings ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Ensure ccache is used if available (improves build times)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Disable C++20 module scanning (fixes the scan-deps error on Clang)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# --- 2. Path Setup ---
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/ext/vulkansdk/x86_64")


# --- 4. Project Structure ---
add_subdirectory(ext)
add_subdirectory(src)
add_subdirectory(app)


if(CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_DATABASE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/compile_commands.json")
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E create_symlink 
            "${CMAKE_DATABASE_OUTPUT_PATH}" 
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Linking compile_commands.json to project root"
    )
endif()
