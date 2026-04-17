cmake_minimum_required(VERSION 3.5)

project(WebsiteEmpireTests LANGUAGES CXX)

enable_testing()

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test Widgets Sql Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Widgets Sql Network)

setup_qcoro(COMPONENTS Core)

# Helper macro to reduce boilerplate for each test target.
macro(add_empire_test TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE
        Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Sql
        QCoro::Core
        -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
    target_include_directories(${TARGET_NAME} PRIVATE ../WebsiteEmpireLib ${CMAKE_CURRENT_SOURCE_DIR})
endmacro()

add_empire_test(Test_Website_PageBlocText       test_website_page_bloc_text.cpp)
add_empire_test(Test_Website_PageBlocImageLinks       test_website_page_bloc_image_links.cpp)
add_empire_test(Test_Website_PageBlocCategoryArticles test_website_page_bloc_image_category.cpp)
add_empire_test(Test_Website_PageTypeArticle  test_page_type_article.cpp)
add_empire_test(Test_Website_Engine_Languages test_website_engine_languages.cpp)
add_empire_test(Test_Website_Engine_Articles  test_website_engine_articles.cpp)
add_empire_test(Test_Website_Settings         test_website_settings.cpp)
add_empire_test(Test_Website_PageTypeRegistry test_page_type_registry.cpp)
add_empire_test(Test_Website_PageDb           test_page_db.cpp)
add_empire_test(Test_Website_PageRepository   test_page_repository.cpp)
add_empire_test(Test_Website_PageGenerator    test_page_generator.cpp)
add_empire_test(Test_Engine_PageBlocJs        test_engine_page_bloc_js.cpp)
add_empire_test(Test_Website_CommonBlocTranslations  test_common_bloc_translations.cpp)
add_empire_test(Test_Website_Translations            test_website_translations.cpp)
add_empire_test(Test_Translation_Settings            test_translation_settings.cpp)
add_empire_test(Test_Translation_PageAssessor        test_translation_page_assessor.cpp)
add_empire_test(Test_Translation_Scheduler           test_translation_scheduler.cpp)
add_empire_test(Test_Translation_StatusTable         test_translation_status_table.cpp)
add_empire_test(Test_Translation_FieldTable          test_translation_field_table.cpp)

# GenStrategyTable test compiles app-layer pane code directly.
add_executable(Test_Website_GenStrategyTable
    test_website_gen_strategy_table.cpp
    ../WebsiteEmpire/gui/panes/GenStrategyTable.cpp)
add_test(NAME Test_Website_GenStrategyTable COMMAND Test_Website_GenStrategyTable)
target_link_libraries(Test_Website_GenStrategyTable PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    QCoro::Core
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_GenStrategyTable PRIVATE
    ../WebsiteEmpireLib
    ../WebsiteEmpire/gui/panes)

# ShortCodes test does not need Sql or Widgets.
add_executable(Test_Website_ShortCodes test_website_shortcodes.cpp)
add_test(NAME Test_Website_ShortCodes COMMAND Test_Website_ShortCodes)
target_link_libraries(Test_Website_ShortCodes PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    QCoro::Core
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_ShortCodes PRIVATE ../WebsiteEmpireLib)
