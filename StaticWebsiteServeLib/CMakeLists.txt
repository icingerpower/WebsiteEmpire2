cmake_minimum_required(VERSION 3.16)
project(StaticWebsiteServeLib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SQLiteCpp is fetched automatically — no manual install needed.
include(FetchContent)
FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG        3.3.2
)
set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SQLiteCpp)

add_library(StaticWebsiteServeLib STATIC
    db/ContentDb.cpp
    db/ImageDb.cpp
    db/StatsDb.cpp
    repository/StatsWriterSQLite.cpp
    repository/PageRepositorySQLite.cpp
    repository/MenuRepositorySQLite.cpp
    repository/RedirectRepositorySQLite.cpp
    repository/ImageRepositorySQLite.cpp
)

target_include_directories(StaticWebsiteServeLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(StaticWebsiteServeLib
    PUBLIC SQLiteCpp
)
