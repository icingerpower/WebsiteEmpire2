cmake_minimum_required(VERSION 3.5)

project(WebsiteEmpire VERSION 0.1 LANGUAGES CXX)

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Sql)

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
    qt_add_executable(WebsiteEmpire
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(WebsiteEmpire SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(WebsiteEmpire
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(WebsiteEmpire PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    QCoro::Core
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)

set_target_properties(WebsiteEmpire PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS WebsiteEmpire
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(WebsiteEmpire)
endif()
