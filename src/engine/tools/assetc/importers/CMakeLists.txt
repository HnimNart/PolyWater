# Only the .cpp is a compile unit; headers are included for IDEs
file(GLOB SRC_IMPORTERS
     CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB HDR_IMPORTERS
     CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.h
     ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

target_sources(engine
  PRIVATE
    ${SRC_IMPORTERS}
    ${HDR_IMPORTERS}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}
             PREFIX "tools/assetc/importers"
             FILES ${SRC_IMPORTERS} ${HDR_IMPORTERS})
