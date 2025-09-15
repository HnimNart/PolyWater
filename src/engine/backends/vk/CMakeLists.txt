# Collect Vulkan backend sources
file(GLOB SRC_VK
     CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB HEADERS_VK
     CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
     ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

# Add to the engine target
target_sources(engine
  PRIVATE
    ${SRC_VK}
    ${HEADERS_VK}
)

# IDE grouping
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
             PREFIX "backends/vk"
             FILES ${SRC_VK} ${HEADERS_VK})
