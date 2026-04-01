include(${CMAKE_CURRENT_LIST_DIR}/attributes/attributes.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/blocs/blocs.cmake)

set(PAGES_FILES
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageType.h
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageType.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageTypeArticle.h
    ${CMAKE_CURRENT_LIST_DIR}/PageTypeArticle.cpp
    ${ATTRIBUTES_FILES}
    ${BLOCS_FILES}
)
