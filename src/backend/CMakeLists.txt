add_library(backend_interfaces INTERFACE)

target_include_directories(backend_interfaces INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interfaces>
)

target_compile_definitions(backend_interfaces INTERFACE
    $<$<CONFIG:Debug>:PROFILE_APP>
    $<$<CONFIG:RelWithDebInfo>:PROFILE_APP>
)



if(APPLE)
    add_subdirectory(metal)
else()
    add_subdirectory(vulkan)
endif()
