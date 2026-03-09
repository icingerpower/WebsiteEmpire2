cmake_minimum_required(VERSION 3.5)

project(WebsiteEmpireTests LANGUAGES CXX)

enable_testing()

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Network)

setup_qcoro(COMPONENTS Core)

add_executable(Test_Website_Blocks test_blocks.cpp)
add_test(NAME Test_Website_Blocks COMMAND Test_Website_Blocks)
target_link_libraries(Test_Website_Blocks PRIVATE Qt${QT_VERSION_MAJOR}::Test QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_Blocks PRIVATE ../WebsiteEmpireLib)
