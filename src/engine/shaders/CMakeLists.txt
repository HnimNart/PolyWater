# Collect shader sources (not compiled by the C++ compiler)
file(GLOB SHADERS
     CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.vert
     ${CMAKE_CURRENT_SOURCE_DIR}/*.frag
     ${CMAKE_CURRENT_SOURCE_DIR}/*.glsl
     ${CMAKE_CURRENT_SOURCE_DIR}/*.rgen
     ${CMAKE_CURRENT_SOURCE_DIR}/*.rchit
     ${CMAKE_CURRENT_SOURCE_DIR}/*.rahit
     ${CMAKE_CURRENT_SOURCE_DIR}/*.rmiss
     ${CMAKE_CURRENT_SOURCE_DIR}/*.rint
     ${CMAKE_CURRENT_SOURCE_DIR}/*.rcall
)

# Add shader files to target for IDE visibility
target_sources(${TARGET} PRIVATE ${SHADERS})

# Group in IDE
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
             PREFIX "shaders"
             FILES ${SHADERS})
