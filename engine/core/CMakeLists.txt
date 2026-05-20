# Core engine library - niezalezne jadro (loadery modeli, zarzadzanie obiektami itp.)
# To nie jest biblioteka sama w sobie, a zbior plikow zrodlowych
# dołączany do engine3d.

target_sources(engine3d PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/object/3ds.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/mediaLibrary.cpp
)

target_include_directories(engine3d PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/object
    ${CMAKE_CURRENT_SOURCE_DIR}
)
