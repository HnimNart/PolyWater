cmake_minimum_required(VERSION 3.22...3.31)
get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})
message(STATUS "Processing: ${PROJECT_NAME}")

# Adding all sources (cpp, hpp, md)
file(GLOB EXE_SOURCES "*.cpp" "*.hpp" "*.md")
source_group("Source Files" FILES ${EXE_SOURCES})

# Handle RT common sources
set(ALL_SOURCES ${EXE_SOURCES})

# Add the executable
add_executable(${PROJECT_NAME} ${ALL_SOURCES})
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER ${PROJECT_NAME})

# Link libraries (consistent across all samples)
target_link_libraries(${PROJECT_NAME} PRIVATE
    nvpro2::nvvk
    nvpro2::nvaftermath
    common
)

# Definitions and Includes
add_project_definitions(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_BINARY_DIR} 
    ${CMAKE_SOURCE_DIR} 
    ${ROOT_DIR}
)

# ------------------------------------------------------------------------------------------------------------------------------
# Installation, copy files
# Build copy files list
set(COPY_FILES ${NsightAftermath_DLLS})


