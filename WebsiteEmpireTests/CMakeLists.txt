cmake_minimum_required(VERSION 3.5)

project(WebsiteEmpireTests LANGUAGES CXX)

enable_testing()

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Widgets Network)

setup_qcoro(COMPONENTS Core)

add_executable(Test_Website_Blocks test_blocks.cpp)
add_test(NAME Test_Website_Blocks COMMAND Test_Website_Blocks)
target_link_libraries(Test_Website_Blocks PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_Blocks PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Website_Engine_Languages test_website_engine_languages.cpp)
add_test(NAME Test_Website_Engine_Languages COMMAND Test_Website_Engine_Languages)
target_link_libraries(Test_Website_Engine_Languages PRIVATE Qt${QT_VERSION_MAJOR}::Test QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_Engine_Languages PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Website_ShortCodes test_website_shortcodes.cpp)
add_test(NAME Test_Website_ShortCodes COMMAND Test_Website_ShortCodes)
target_link_libraries(Test_Website_ShortCodes PRIVATE Qt${QT_VERSION_MAJOR}::Test QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_ShortCodes PRIVATE ../WebsiteEmpireLib)
