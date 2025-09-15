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
target_sources(engine PRIVATE ${SHADERS})

# Group in IDE
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
             PREFIX "shaders"
             FILES ${SHADERS})

# ----- Compile GLSL → SPIR-V via your helper -----
# Mirrors your original top-level logic
set(SPV_OUT_DIR ${CMAKE_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${SPV_OUT_DIR})

# compile_glsl_directory(...) is assumed to be defined elsewhere in your build
compile_glsl_directory(
  SRC "${CMAKE_CURRENT_SOURCE_DIR}"
  DST "${SPV_OUT_DIR}"
  VULKAN_TARGET "vulkan1.2"
  DEPENDENCY ${VULKAN_BUILD_DEPENDENCIES}
)
