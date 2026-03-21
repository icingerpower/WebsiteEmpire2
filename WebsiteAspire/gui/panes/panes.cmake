include(${CMAKE_CURRENT_LIST_DIR}/generator/generator.cmake)

set(PANES_FILES
    ${CMAKE_CURRENT_LIST_DIR}/PaneAspire.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PaneAspire.h
    ${CMAKE_CURRENT_LIST_DIR}/PaneAspire.ui
    ${CMAKE_CURRENT_LIST_DIR}/PaneDatabaseGen.cpp
    ${CMAKE_CURRENT_LIST_DIR}/PaneDatabaseGen.h
    ${CMAKE_CURRENT_LIST_DIR}/PaneDatabaseGen.ui
    ${CMAKE_CURRENT_LIST_DIR}/WidgetGenerator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/WidgetGenerator.h
    ${CMAKE_CURRENT_LIST_DIR}/WidgetGenerator.ui
    ${GENERATOR_PANE_FILES}
)
