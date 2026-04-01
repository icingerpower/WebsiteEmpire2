cmake_minimum_required(VERSION 3.16)
project(StaticWebsiteServe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Install Drogon from source (run these commands in a terminal):
#
#   sudo apt install libjsoncpp-dev uuid-dev libssl-dev zlib1g-dev
#
#   git clone https://github.com/drogonframework/drogon
#   cd drogon
#   git submodule update --init
#   mkdir build
#   cd build
#   cmake ..
#   make -j$(nproc)
#   sudo make install
#
find_package(Drogon REQUIRED)

add_executable(StaticWebsiteServe
    main.cpp
    controllers/PageController.cpp
    controllers/ImageController.cpp
    controllers/StatsController.cpp
)

target_link_libraries(StaticWebsiteServe
    PRIVATE Drogon::Drogon StaticWebsiteServeLib
)

target_include_directories(StaticWebsiteServe
    PRIVATE ../StaticWebsiteServeLib
)
