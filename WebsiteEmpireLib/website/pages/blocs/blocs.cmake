include(${CMAKE_CURRENT_LIST_DIR}/widgets/widgets.cmake)

set(BLOCS_FILES
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageBloc.h
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageBloc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocText.h
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocText.cpp
    ${BLOCS_WIDGETS_FILES}
)
