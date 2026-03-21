include(${CMAKE_CURRENT_LIST_DIR}/attributes/attributes.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/downloader/downloader.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/generator/generator.cmake)

set(ASPIRE_FILES
    ${ASPIRE_ATTRIBUTES_FILES}
    ${ASPIRE_DOWNLOADER_FILES}
    ${ASPIRE_GENERATOR_FILES}
    ${CMAKE_CURRENT_LIST_DIR}/AspiredDb.h
    ${CMAKE_CURRENT_LIST_DIR}/AspiredDb.cpp
)
