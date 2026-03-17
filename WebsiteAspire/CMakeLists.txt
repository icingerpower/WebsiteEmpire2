cmake_minimum_required(VERSION 3.5)

project(WebsiteAspire VERSION 0.1 LANGUAGES CXX)

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

setup_qcoro(COMPONENTS Core)

include(../../common/workingdirectory/workingdirectory.cmake)

include(gui/gui.cmake)
include(launcher/launcher.cmake)

set(PROJECT_SOURCES
    main.cpp
    "../../common/types/types.h"
    "../../common/types/types.cpp"
    ${WORKING_DIR_FILES}
    ${GUI_FILES}
    ${LAUNCHER_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(WebsiteAspire
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        gui/panes/PaneAspire.h gui/panes/PaneAspire.cpp gui/panes/PaneAspire.ui
        gui/panes/WidgetDownloader.h gui/panes/WidgetDownloader.cpp gui/panes/WidgetDownloader.ui
    )
else()
    if(ANDROID)
        add_library(WebsiteAspire SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(WebsiteAspire
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(WebsiteAspire PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    QCoro::Core
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)

set_target_properties(WebsiteAspire PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS WebsiteAspire
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(WebsiteAspire)
endif()
