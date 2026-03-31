cmake_minimum_required(VERSION 3.14)

project(WebsiteEmpireLib LANGUAGES CXX)

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Sql Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Sql Network)

setup_qcoro(COMPONENTS Network Core)

include(../../common/utils/utils.cmake)

include(aspire/aspire.cmake)
include(website/website.cmake)

add_library(WebsiteEmpireLib STATIC
    WebsiteEmpireLib.cpp
    WebsiteEmpireLib.h
    ExceptionWithTitleText.h
    ExceptionWithTitleText.cpp
    CountryLangManager.h
    CountryLangManager.cpp
    ${ASPIRE_FILES}
    ${WEBSITE_FILES}
    ${UTILS_FILES}
)

target_link_libraries(WebsiteEmpireLib
    PUBLIC
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Network
    QCoro::Network
    QCoro::Core
)

target_include_directories(WebsiteEmpireLib PUBLIC ../../common ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(WebsiteEmpireLib PRIVATE WEBSITEEMPIRELIB_LIBRARY)
