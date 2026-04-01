include(${CMAKE_CURRENT_LIST_DIR}/attributes/attributes.cmake)

set(PAGES_FILES
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageBloc.h
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageBloc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageBlockWidget.h
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageBlockWidget.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlockText.h
    ${CMAKE_CURRENT_LIST_DIR}/PageBlockText.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlockTextWidget.h
    ${CMAKE_CURRENT_LIST_DIR}/PageBlockTextWidget.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlockTextWidget.ui
    ${ATTRIBUTES_FILES}
)
