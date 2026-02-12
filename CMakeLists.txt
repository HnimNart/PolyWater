cmake_minimum_required(VERSION 3.22...3.31)
project(my_render VERSION 1.0 LANGUAGES C CXX)

# Here we can set the Slang version to use (uncomment to override the default)
# set(Slang_VERSION "2025.20" CACHE STRING "Version of Slang to use")

# Include the RtTutorial common functions
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/ext/vulkansdk/x86_64")

# Add the cmake folder to the module path
set(NVPRO_CORE2_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(NvproCore2 REQUIRED)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
    add_compile_options(/w)
else()
    add_compile_options(-w)
endif()

# Add common library first as other samples might depend on it
add_subdirectory(src)
add_subdirectory(app)


# Install the resources and nvshaders folders
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders" DESTINATION ${CMAKE_INSTALL_PREFIX}/src)
install(DIRECTORY "${NVSHADERS_DIR}/nvshaders" DESTINATION ${CMAKE_INSTALL_PREFIX})
install(FILES ${Slang_GLSLANG} DESTINATION ${CMAKE_INSTALL_PREFIX})
