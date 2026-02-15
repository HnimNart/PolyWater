set(LIB_NAME scene_lib)

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS 
    "*.cpp" 
    "*.hpp" 
    "*.h"
)

add_library(${LIB_NAME} SHARED ${PLUGIN_SOURCES})


# -------------------------------------------------------------------
# 3. Linking & Include logic
# -------------------------------------------------------------------
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/>
        "${ROOT_DIR}/ext/tinygltf"
        "${ROOT_DIR}/ext/stb"
    PRIVATE
        ${SHADER_OUTPUT_DIR}/../
        ${SHADER_OUTPUT_DIR}

)

target_link_libraries(${LIB_NAME}
    PUBLIC
        backend_interfaces  
        core_lib
        tinyobjloader
        bvh
)



