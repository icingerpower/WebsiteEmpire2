cmake_minimum_required(VERSION 3.5)

project(WebsiteAspireTests LANGUAGES CXX)

enable_testing()

include(../common.cmake)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Network Sql)

setup_qcoro(COMPONENTS Core)

add_executable(Test_Aspire_Page_Attributes test_page_attributes.cpp)
add_test(NAME Test_Aspire_Page_Attributes COMMAND Test_Aspire_Page_Attributes)
target_link_libraries(Test_Aspire_Page_Attributes PRIVATE Qt${QT_VERSION_MAJOR}::Test QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Aspire_Page_Attributes PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Aspired_Db test_aspired_db.cpp)
add_test(NAME Test_Aspired_Db COMMAND Test_Aspired_Db)
target_link_libraries(Test_Aspired_Db PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Sql QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Aspired_Db PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Downloaded_Pages_Table test_downloaded_pages_table.cpp)
add_test(NAME Test_Downloaded_Pages_Table COMMAND Test_Downloaded_Pages_Table)
target_link_libraries(Test_Downloaded_Pages_Table PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Sql QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Downloaded_Pages_Table PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Downloader_Vogelvoerkopen test_downloader_vogelvoerkopen.cpp)
add_test(NAME Test_Downloader_Vogelvoerkopen COMMAND Test_Downloader_Vogelvoerkopen)
set_tests_properties(Test_Downloader_Vogelvoerkopen PROPERTIES TIMEOUT 700)
target_link_libraries(Test_Downloader_Vogelvoerkopen PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Network QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Downloader_Vogelvoerkopen PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Downloader_Pradize test_downloader_pradize.cpp)
add_test(NAME Test_Downloader_Pradize COMMAND Test_Downloader_Pradize)
set_tests_properties(Test_Downloader_Pradize PROPERTIES TIMEOUT 700)
target_link_libraries(Test_Downloader_Pradize PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Sql Qt${QT_VERSION_MAJOR}::Network QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Downloader_Pradize PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Generator_Factories test_generator_factories.cpp)
add_test(NAME Test_Generator_Factories COMMAND Test_Generator_Factories)
target_link_libraries(Test_Generator_Factories PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Sql QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Generator_Factories PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Generator_Languages test_generator_languages.cpp)
add_test(NAME Test_Generator_Languages COMMAND Test_Generator_Languages)
target_link_libraries(Test_Generator_Languages PRIVATE Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Sql QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Generator_Languages PRIVATE ../WebsiteEmpireLib)

add_executable(Test_Params_Model test_params_model.cpp)
add_test(NAME Test_Params_Model COMMAND Test_Params_Model)
target_link_libraries(Test_Params_Model PRIVATE Qt${QT_VERSION_MAJOR}::Test QCoro::Core -Wl,--whole-archive WebsiteEmpireLib -Wl,--no-whole-archive)
target_include_directories(Test_Params_Model PRIVATE ../WebsiteEmpireLib)
