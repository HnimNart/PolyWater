# -------------------------------------------------------------------
# App: apples  (Metal backend, Apple platforms only)
# -------------------------------------------------------------------
if(NOT APPLE)
  return()
endif()

cmake_minimum_required(VERSION 3.22...3.31)

get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME} LANGUAGES C CXX)
message(STATUS "Processing App: ${PROJECT_NAME}")

# 2. Sources
file(GLOB EXE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp")

# 3. Define Executable
add_executable(${PROJECT_NAME} ${EXE_SOURCES})

# 4. Link Dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE
    metal_backend
    common
)

# 5. IDE Organization
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Samples")
source_group("Source Files" FILES ${EXE_SOURCES})

# 6. Include Directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}
)
