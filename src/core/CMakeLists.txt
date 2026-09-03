set(LIB_NAME core_lib)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS 
    "*.cpp" 
    "*.hpp" 
    "*.h"
)

add_library(${LIB_NAME} SHARED ${CORE_SOURCES})

target_link_libraries(${LIB_NAME}
    PUBLIC
        glm          
        fmt

)

target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        ${ROOT_DIR}/ext/tinygltf
        ${ROOT_DIR}/ext/stb
        ${ROOT_DIR}/ext
)

target_compile_definitions(${LIB_NAME} PUBLIC 
    ROOT_DIR=\"${CMAKE_SOURCE_DIR}/\"
)

