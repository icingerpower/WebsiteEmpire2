include(${CMAKE_CURRENT_LIST_DIR}/widgets/widgets.cmake)

set(BLOCS_FILES
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageBloc.h
    ${CMAKE_CURRENT_LIST_DIR}/AbstractPageBloc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocText.h
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocText.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocCategory.h
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocCategory.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocImageLinks.h
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocImageLinks.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocCategoryArticles.h
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocCategoryArticles.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocJs.h
    ${CMAKE_CURRENT_LIST_DIR}/PageBlocJs.cpp
    ${BLOCS_WIDGETS_FILES}
)
