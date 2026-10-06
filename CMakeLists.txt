cmake_minimum_required(VERSION 3.11)
project(delivery_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_TSAN "Instrumentar delivery_sim con ThreadSanitizer" OFF)

find_package(Threads REQUIRED)

add_executable(delivery_sim src/main.cpp)

# nlohmann/json (header unico, MIT) vive en third_party/. Se incluye como SYSTEM
# para que sus cabeceras no generen warnings bajo -Wall -Wextra.
target_include_directories(delivery_sim SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

target_compile_options(delivery_sim PRIVATE -Wall -Wextra)
target_link_libraries(delivery_sim PRIVATE Threads::Threads)

if(ENABLE_TSAN)
  target_compile_options(delivery_sim PRIVATE -fsanitize=thread -g)
  target_link_libraries(delivery_sim PRIVATE -fsanitize=thread)
endif()
