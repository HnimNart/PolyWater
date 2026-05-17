# -------------------------------------------------------------------
# App: new_world 
# -------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22...3.31)

get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME} LANGUAGES C CXX)
message(STATUS "Processing App: ${PROJECT_NAME}")

file(GLOB EXE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.md")

add_executable(${PROJECT_NAME} ${EXE_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    vk_render
    common             
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  $<$<CONFIG:RelWithDebInfo>:PROFILE_APP>  
  $<$<CONFIG:Debug>:PROFILE_APP>  
)

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Samples")
source_group("Source Files" FILES ${EXE_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_BINARY_DIR}
    ${Slang_INCLUDE_DIR}
)

if(NsightAftermath_DLLS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${NsightAftermath_DLLS}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()
