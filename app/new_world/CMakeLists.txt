# -------------------------------------------------------------------
# App: new_world 
# -------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22...3.31)

# 1. Setup Project
get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME} LANGUAGES C CXX)
message(STATUS "Processing App: ${PROJECT_NAME}")

# 2. Sources
file(GLOB EXE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.md")

# 3. Define Executable
add_executable(${PROJECT_NAME} ${EXE_SOURCES})

# 4. Link Dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE
    vk_render
    common             
)

# 5. IDE Organization
# Groups this project under a "Samples" folder in Visual Studio/Xcode
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Samples")
source_group("Source Files" FILES ${EXE_SOURCES})

# 6. Include Directories
# Only add specific includes if they are NOT already in 'common'.
# (Binary dir is usually needed for generated shaders/headers)
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_BINARY_DIR}
    ${Slang_INCLUDE_DIR}
)

# 7. Post-Build / Install (Optional)
# If you need to copy Nsight DLLs or other assets next to the binary:
if(NsightAftermath_DLLS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${NsightAftermath_DLLS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()
