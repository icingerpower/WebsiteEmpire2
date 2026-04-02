include(${CMAKE_CURRENT_LIST_DIR}/shortcodes/shortcodes.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/pages/pages.cmake)

set(WEBSITE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/WebCodeAdder.h
    ${CMAKE_CURRENT_LIST_DIR}/AbstractBlock.h
    ${CMAKE_CURRENT_LIST_DIR}/AbstractBlock.cpp
    ${CMAKE_CURRENT_LIST_DIR}/AbstractEngine.h
    ${CMAKE_CURRENT_LIST_DIR}/AbstractEngine.cpp
    ${CMAKE_CURRENT_LIST_DIR}/HostTable.h
    ${CMAKE_CURRENT_LIST_DIR}/HostTable.cpp
    ${CMAKE_CURRENT_LIST_DIR}/EngineLanguages.h
    ${CMAKE_CURRENT_LIST_DIR}/EngineLanguages.cpp
    ${CMAKE_CURRENT_LIST_DIR}/EngineArticles.h
    ${CMAKE_CURRENT_LIST_DIR}/EngineArticles.cpp
    ${SHORTCODES_FILES}
    ${PAGES_FILES}
)
