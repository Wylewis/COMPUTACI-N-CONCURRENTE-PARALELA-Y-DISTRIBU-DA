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

add_executable(delivery_sim
  src/main.cpp
  src/config.cpp
  src/order.cpp
  src/sim_clock.cpp
)

# nlohmann/json (header unico, MIT) vive en third_party/. Se incluye como SYSTEM
# para que sus cabeceras no generen warnings bajo -Wall -Wextra.
target_include_directories(delivery_sim SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

target_compile_options(delivery_sim PRIVATE -Wall -Wextra)
target_link_libraries(delivery_sim PRIVATE Threads::Threads)

if(ENABLE_TSAN)
  target_compile_options(delivery_sim PRIVATE -fsanitize=thread -g)
  target_link_libraries(delivery_sim PRIVATE -fsanitize=thread)
endif()

# Pruebas unitarias: se construyen con `make` y se ejecutan con `ctest`.
enable_testing()

add_executable(order_test tests/order_test.cpp src/order.cpp)
target_include_directories(order_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(order_test PRIVATE -Wall -Wextra)
add_test(NAME order_test COMMAND order_test)

add_executable(sim_clock_test tests/sim_clock_test.cpp src/sim_clock.cpp)
target_include_directories(sim_clock_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(sim_clock_test PRIVATE -Wall -Wextra)
add_test(NAME sim_clock_test COMMAND sim_clock_test)

# Carga y validacion del config: cada archivo de tests/invalid/ debe rechazarse
# con estado distinto de cero y un mensaje; tests/config/minimal.json debe cargar.
add_test(NAME config_tests
         COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_config_tests.sh $<TARGET_FILE:delivery_sim>)
