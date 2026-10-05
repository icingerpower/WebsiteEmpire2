cmake_minimum_required(VERSION 3.5)

project(WebsiteEmpireTests LANGUAGES CXX)

enable_testing()

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test Widgets Sql Network Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Widgets Sql Network Svg)
find_package(ZLIB REQUIRED)

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

add_empire_test(Test_Website_PageBlocText              test_website_page_bloc_text.cpp)
add_empire_test(Test_Website_PageBlocMeta              test_website_page_bloc_meta.cpp)
add_empire_test(Test_Website_PageBlocCategory          test_website_page_bloc_category.cpp)
add_empire_test(Test_Website_PageBlocCategoryLinks     test_website_page_bloc_category_links.cpp)
add_empire_test(Test_Website_PageBlocImageLinks        test_website_page_bloc_image_links.cpp)
add_empire_test(Test_Website_PageBlocCategoryArticles test_website_page_bloc_image_category.cpp)
add_empire_test(Test_Website_PageTypeArticleHealth  test_page_type_article_health.cpp)
add_empire_test(Test_Website_Engine_Languages test_website_engine_languages.cpp)
add_empire_test(Test_Website_Engine_Articles  test_website_engine_articles.cpp)
add_empire_test(Test_Website_Settings         test_website_settings.cpp)
add_empire_test(Test_Website_PageTypeRegistry test_page_type_registry.cpp)
add_empire_test(Test_Website_PageDb           test_page_db.cpp)
add_empire_test(Test_Website_PageRepository   test_page_repository.cpp)
add_empire_test(Test_Website_PageDataPreservation test_page_data_preservation.cpp)
add_empire_test(Test_Website_ArticleExcerpt   test_website_article_excerpt.cpp)
add_empire_test(Test_Website_PageGenerator    test_page_generator.cpp)
target_link_libraries(Test_Website_PageGenerator PRIVATE ZLIB::ZLIB)
add_empire_test(Test_Engine_PageBlocJs        test_engine_page_bloc_js.cpp)
add_empire_test(Test_Website_CommonBlocTranslations  test_common_bloc_translations.cpp)
add_empire_test(Test_Website_PageLabels test_page_labels.cpp)
add_empire_test(Test_Website_Translations            test_website_translations.cpp)
add_empire_test(Test_Website_CommonBlocMenuPermalink test_website_commonbloc_menu_permalink.cpp)
add_empire_test(Test_Website_Translation_Settings            test_translation_settings.cpp)
add_empire_test(Test_Website_Translation_PageAssessor        test_translation_page_assessor.cpp)
add_empire_test(Test_Website_Translation_Scheduler           test_translation_scheduler.cpp)
add_empire_test(Test_Website_Translation_StatusTable         test_translation_status_table.cpp)
add_empire_test(Test_Website_Translation_FieldTable          test_translation_field_table.cpp)
add_empire_test(Test_Website_Translation_Protocol            test_translation_protocol.cpp)
add_empire_test(Test_Website_GenerationWorkflow              test_generation_workflow.cpp)
add_empire_test(Test_Website_GenerationCoroutine             test_generation_coroutine.cpp)
add_empire_test(Test_Website_CategoryHub_DirtySet            test_category_hub_dirty_set.cpp)
add_empire_test(Test_Website_CategoryHub_Syncer              test_category_hub_syncer.cpp)
add_empire_test(Test_Website_FashionTaxonomyHub_Syncer        test_fashion_taxonomy_hub_syncer.cpp)
add_empire_test(Test_Website_PageTypeFashionTagHub            test_page_type_fashion_tag_hub.cpp)
add_empire_test(Test_Website_PageBlocFashionHubGrid           test_page_bloc_fashion_hub_grid.cpp)
add_empire_test(Test_Website_SocialMedia                     test_social_media.cpp)
add_empire_test(Test_Website_ReviewAction                    test_website_review_action.cpp)
add_empire_test(Test_Website_PageStateFormatter              test_website_page_state_formatter.cpp)
add_empire_test(Test_Website_SitemapChunkWriter              test_website_sitemap_chunk_writer.cpp)
add_empire_test(Test_Website_SitemapIndexWriter              test_website_sitemap_index_writer.cpp)
add_empire_test(Test_Website_RobotsWriter                    test_website_robots_writer.cpp)
target_link_libraries(Test_Website_RobotsWriter PRIVATE ZLIB::ZLIB)
add_empire_test(Test_Website_SitemapOrchestrator             test_website_sitemap_orchestrator.cpp)
add_empire_test(Test_Website_StatsDbMerger                   test_stats_db_merger.cpp)
add_empire_test(Test_Website_PagesStatsWidget                test_pages_stats_widget.cpp)
add_empire_test(Test_Website_CliAntigravity                  test_cli_antigravity.cpp)
add_empire_test(Test_Website_RasterCliProtocol                 test_raster_cli_protocol.cpp)
add_empire_test(Test_Website_CliCodex                        test_cli_codex.cpp)
add_empire_test(Test_Website_GenPageQueueRaster               test_gen_page_queue_raster.cpp)
add_empire_test(Test_Website_AbstractPageBlocTaxonomies       test_abstract_page_bloc_taxonomies.cpp)
add_empire_test(Test_Website_PageBlocFashionTaxonomyLinks     test_page_bloc_fashion_taxonomy_links.cpp)
add_empire_test(Test_Website_TaxonomyTranslationFilter        test_taxonomy_translation_filter.cpp)
add_empire_test(Test_Website_PageTypeArticleFashion            test_page_type_article_fashion.cpp)
add_empire_test(Test_Website_EngineArticlesFashion             test_engine_articles_fashion.cpp)
add_empire_test(Test_Website_RasterImageTracking              test_raster_image_tracking.cpp)
add_empire_test(Test_Website_VerticalSyncPolicy                 test_vertical_sync_policy.cpp)

# LauncherTranslate modes test compiles launcher code directly (no app binary needed).
add_executable(Test_Website_LauncherTranslate_Modes
    test_launcher_translate_modes.cpp
    ../WebsiteEmpire/launcher/LauncherTranslate.cpp
    ../WebsiteEmpire/launcher/AbstractLauncher.cpp
    ../../common/workingdirectory/WorkingDirectoryManager.cpp)
add_test(NAME Test_Website_LauncherTranslate_Modes COMMAND Test_Website_LauncherTranslate_Modes)
target_link_libraries(Test_Website_LauncherTranslate_Modes PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    QCoro::Core
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_LauncherTranslate_Modes PRIVATE
    ../WebsiteEmpireLib
    ../WebsiteEmpire/launcher
    ${CMAKE_CURRENT_SOURCE_DIR})

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

# PaneTaxonomies test compiles app-layer pane code directly (no .ui file —
# the widget is built entirely in code).
add_executable(Test_Website_PaneTaxonomies
    test_pane_taxonomies.cpp
    ../WebsiteEmpire/gui/panes/PaneTaxonomies.cpp)
add_test(NAME Test_Website_PaneTaxonomies COMMAND Test_Website_PaneTaxonomies)
target_link_libraries(Test_Website_PaneTaxonomies PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    QCoro::Core
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_PaneTaxonomies PRIVATE
    ../WebsiteEmpireLib
    ../WebsiteEmpire)

# SVG validation test — needs Qt::Svg.
add_executable(Test_Website_SvgValidation test_website_svg_validation.cpp)
add_test(NAME Test_Website_SvgValidation COMMAND Test_Website_SvgValidation)
target_link_libraries(Test_Website_SvgValidation PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Svg
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_SvgValidation PRIVATE ../WebsiteEmpireLib)

# ShortCodes test does not need Sql or Widgets.
add_executable(Test_Website_ShortCodes test_website_shortcodes.cpp)
add_test(NAME Test_Website_ShortCodes COMMAND Test_Website_ShortCodes)
target_link_libraries(Test_Website_ShortCodes PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    QCoro::Core
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_ShortCodes PRIVATE ../WebsiteEmpireLib)

# ArticleContentValidator test does not need Sql or Widgets.
add_executable(Test_Website_ArticleContentValidator test_website_article_content_validator.cpp)
add_test(NAME Test_Website_ArticleContentValidator COMMAND Test_Website_ArticleContentValidator)
target_link_libraries(Test_Website_ArticleContentValidator PRIVATE
    Qt${QT_VERSION_MAJOR}::Test
    QCoro::Core
    -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Website_ArticleContentValidator PRIVATE ../WebsiteEmpireLib)
