include(${CMAKE_CURRENT_LIST_DIR}/panes/panes.cmake)

set(GUI_FILES
    ${CMAKE_CURRENT_LIST_DIR}/MainWindow.cpp
    ${CMAKE_CURRENT_LIST_DIR}/MainWindow.h
    ${CMAKE_CURRENT_LIST_DIR}/MainWindow.ui
    ${PANES_FILES}
)
