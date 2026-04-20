# -------------------------------------------------------------------
# App: minimal_app
# A minimal application that runs the application loop without
# rendering anything, ensuring all dependencies are met.
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
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Samples")
source_group("Source Files" FILES ${EXE_SOURCES})

# 6. Include Directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_BINARY_DIR}
)

# 7. Post-Build / Install (Optional)
if(NsightAftermath_DLLS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${NsightAftermath_DLLS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()
