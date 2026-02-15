# -------------------------------------------------------------------
# Library: core_lib (Shared)
# -------------------------------------------------------------------
set(LIB_NAME core_lib)

# 1. Collect Sources
# Using GLOB_RECURSE to pick up shape/primitives.cpp and others
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS 
    "*.cpp" 
    "*.hpp" 
    "*.h"
)

# 2. Define Shared Library
add_library(${LIB_NAME} SHARED ${CORE_SOURCES})

# 3. Essential Properties for Shared Libs
set_target_properties(${LIB_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

find_package(fmt REQUIRED)
find_package(glm REQUIRED)
target_link_libraries(${LIB_NAME}
    PUBLIC
        glm          
        fmt     
)

target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_compile_definitions(${LIB_NAME} PUBLIC 
    ROOT_DIR=\"${CMAKE_SOURCE_DIR}/\"
)

