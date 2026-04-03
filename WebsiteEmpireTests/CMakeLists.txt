cmake_minimum_required(VERSION 3.5)

project(WebsiteEmpireTests LANGUAGES CXX)

enable_testing()

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Widgets Network)

setup_qcoro(COMPONENTS Core)

add_executable(Test_Website_PageBlocText test_website_page_bloc_text.cpp)
add_test(NAME Test_Website_PageBlocText COMMAND Test_Website_PageBlocText)
target_link_libraries(Test_Website_PageBlocText PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_PageBlocText PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Website_PageTypeArticle test_page_type_article.cpp)
add_test(NAME Test_Website_PageTypeArticle COMMAND Test_Website_PageTypeArticle)
target_link_libraries(Test_Website_PageTypeArticle PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_PageTypeArticle PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Website_Engine_Languages test_website_engine_languages.cpp)
add_test(NAME Test_Website_Engine_Languages COMMAND Test_Website_Engine_Languages)
target_link_libraries(Test_Website_Engine_Languages PRIVATE Qt${QT_VERSION_MAJOR}::Test QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_Engine_Languages PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Website_ShortCodes test_website_shortcodes.cpp)
add_test(NAME Test_Website_ShortCodes COMMAND Test_Website_ShortCodes)
target_link_libraries(Test_Website_ShortCodes PRIVATE Qt${QT_VERSION_MAJOR}::Test QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_ShortCodes PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Website_Engine_Articles test_website_engine_articles.cpp)
add_test(NAME Test_Website_Engine_Articles COMMAND Test_Website_Engine_Articles)
target_link_libraries(Test_Website_Engine_Articles PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_Engine_Articles PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Website_Settings test_website_settings.cpp)
add_test(NAME Test_Website_Settings COMMAND Test_Website_Settings)
target_link_libraries(Test_Website_Settings PRIVATE Qt${QT_VERSION_MAJOR}::Test QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_Settings PRIVATE ../WebsiteEmpireLib)
