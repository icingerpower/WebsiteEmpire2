cmake_minimum_required(VERSION 3.16)
project(StaticWebsiteServeTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon REQUIRED)

add_executable(Test_StaticServe_Db
    test_content_db.cpp
)

target_link_libraries(Test_StaticServe_Db
    PRIVATE Drogon::Drogon StaticWebsiteServeLib
)

target_include_directories(Test_StaticServe_Db
    PRIVATE ../StaticWebsiteServeLib
)

add_test(NAME Test_StaticServe_Db COMMAND Test_StaticServe_Db)

add_executable(Test_StaticServe_Repositories
    test_repositories.cpp
)

target_link_libraries(Test_StaticServe_Repositories
    PRIVATE Drogon::Drogon StaticWebsiteServeLib
)

target_include_directories(Test_StaticServe_Repositories
    PRIVATE ../StaticWebsiteServeLib
)

add_test(NAME Test_StaticServe_Repositories COMMAND Test_StaticServe_Repositories)

add_executable(Test_StaticServe_StatsWriter
    test_stats_writer.cpp
)

target_link_libraries(Test_StaticServe_StatsWriter
    PRIVATE Drogon::Drogon StaticWebsiteServeLib
)

target_include_directories(Test_StaticServe_StatsWriter
    PRIVATE ../StaticWebsiteServeLib
)

add_test(NAME Test_StaticServe_StatsWriter COMMAND Test_StaticServe_StatsWriter)
