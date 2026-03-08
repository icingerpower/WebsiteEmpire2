cmake_minimum_required(VERSION 3.5)

project(WebsiteAspireTests LANGUAGES CXX)

enable_testing()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

include(../common.cmake)

add_executable(Test_Aspire_Page_Attributes test_page_attributes.cpp)
add_test(NAME Test_Aspire_Page_Attributes COMMAND Test_Aspire_Page_Attributes)
target_link_libraries(Test_Aspire_Page_Attributes PRIVATE Qt${QT_VERSION_MAJOR}::Test -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Aspire_Page_Attributes PRIVATE ../WebsiteEmpireLib)
