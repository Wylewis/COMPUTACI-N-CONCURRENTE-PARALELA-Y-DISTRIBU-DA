cmake_minimum_required(VERSION 3.11)
project(delivery_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_TSAN "Instrumentar delivery_sim con ThreadSanitizer" OFF)
option(ENABLE_ASAN "Instrumentar delivery_sim con AddressSanitizer (y LeakSanitizer donde exista)" OFF)

if(ENABLE_TSAN AND ENABLE_ASAN)
  message(FATAL_ERROR "ENABLE_TSAN y ENABLE_ASAN no pueden combinarse: los sanitizers de hilos y de memoria son incompatibles entre si")
endif()

# Aplica el sanitizer elegido a un target. Se usa para delivery_sim y para
# cada prueba, asi `ctest` corre instrumentado con la misma opcion.
function(apply_sanitizers target)
  if(ENABLE_TSAN)
    target_compile_options(${target} PRIVATE -fsanitize=thread -g)
    target_link_libraries(${target} PRIVATE -fsanitize=thread)
  elseif(ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_libraries(${target} PRIVATE -fsanitize=address)
  endif()
endfunction()

find_package(Threads REQUIRED)

add_executable(delivery_sim
  src/main.cpp
  src/config.cpp
  src/order.cpp
  src/sim_clock.cpp
  src/event_log.cpp
  src/graph.cpp
  src/order_generator.cpp
  src/route_motion.cpp
  src/estimate.cpp
  src/distance_table.cpp
  src/distance_table_parallel.cpp
  src/map_projection.cpp
  src/thread_pool.cpp
  src/hardware.cpp
  src/stop_signal.cpp
)

# nlohmann/json (header unico, MIT) vive en third_party/. Se incluye como SYSTEM
# para que sus cabeceras no generen warnings bajo -Wall -Wextra.
target_include_directories(delivery_sim SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

target_compile_options(delivery_sim PRIVATE -Wall -Wextra)
target_link_libraries(delivery_sim PRIVATE Threads::Threads)
apply_sanitizers(delivery_sim)

# Pruebas unitarias: se construyen con `make` y se ejecutan con `ctest`.
enable_testing()

add_executable(order_test tests/order_test.cpp src/order.cpp)
target_include_directories(order_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(order_test PRIVATE -Wall -Wextra)
apply_sanitizers(order_test)
add_test(NAME order_test COMMAND order_test)

add_executable(sim_clock_test tests/sim_clock_test.cpp src/sim_clock.cpp)
target_include_directories(sim_clock_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(sim_clock_test PRIVATE -Wall -Wextra)
apply_sanitizers(sim_clock_test)
add_test(NAME sim_clock_test COMMAND sim_clock_test)

add_executable(event_log_test tests/event_log_test.cpp src/event_log.cpp src/order.cpp)
target_include_directories(event_log_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(event_log_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(event_log_test PRIVATE -Wall -Wextra)
apply_sanitizers(event_log_test)
add_test(NAME event_log_test COMMAND event_log_test)

add_executable(graph_test tests/graph_test.cpp src/graph.cpp src/config.cpp)
target_include_directories(graph_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(graph_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(graph_test PRIVATE -Wall -Wextra)
apply_sanitizers(graph_test)
add_test(NAME graph_test COMMAND graph_test ${CMAKE_SOURCE_DIR}/config/equipetrol.json)

add_executable(order_generator_test tests/order_generator_test.cpp
  src/order_generator.cpp src/order.cpp src/graph.cpp src/config.cpp)
target_include_directories(order_generator_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(order_generator_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(order_generator_test PRIVATE -Wall -Wextra)
apply_sanitizers(order_generator_test)
add_test(NAME order_generator_test COMMAND order_generator_test ${CMAKE_SOURCE_DIR}/config/equipetrol.json)

add_executable(route_motion_test tests/route_motion_test.cpp src/route_motion.cpp src/graph.cpp src/config.cpp)
target_include_directories(route_motion_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(route_motion_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(route_motion_test PRIVATE -Wall -Wextra)
apply_sanitizers(route_motion_test)
add_test(NAME route_motion_test COMMAND route_motion_test ${CMAKE_SOURCE_DIR}/config/equipetrol.json)

add_executable(estimate_test tests/estimate_test.cpp
  src/estimate.cpp src/route_motion.cpp src/order.cpp src/graph.cpp src/config.cpp)
target_include_directories(estimate_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(estimate_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(estimate_test PRIVATE -Wall -Wextra)
apply_sanitizers(estimate_test)
add_test(NAME estimate_test COMMAND estimate_test ${CMAKE_SOURCE_DIR}/config/equipetrol.json)

add_executable(distance_table_test tests/distance_table_test.cpp src/distance_table.cpp src/graph.cpp src/config.cpp)
target_include_directories(distance_table_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(distance_table_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(distance_table_test PRIVATE -Wall -Wextra)
apply_sanitizers(distance_table_test)
add_test(NAME distance_table_test COMMAND distance_table_test ${CMAKE_SOURCE_DIR}/config/equipetrol.json)

add_executable(distance_table_parallel_test tests/distance_table_parallel_test.cpp
  src/distance_table_parallel.cpp src/distance_table.cpp src/graph.cpp src/config.cpp src/hardware.cpp)
target_include_directories(distance_table_parallel_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(distance_table_parallel_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(distance_table_parallel_test PRIVATE -Wall -Wextra)
target_link_libraries(distance_table_parallel_test PRIVATE Threads::Threads)
apply_sanitizers(distance_table_parallel_test)
add_test(NAME distance_table_parallel_test COMMAND distance_table_parallel_test ${CMAKE_SOURCE_DIR}/config/equipetrol.json)

add_executable(map_projection_test tests/map_projection_test.cpp src/map_projection.cpp src/graph.cpp src/config.cpp)
target_include_directories(map_projection_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(map_projection_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(map_projection_test PRIVATE -Wall -Wextra)
apply_sanitizers(map_projection_test)
add_test(NAME map_projection_test COMMAND map_projection_test ${CMAKE_SOURCE_DIR}/config/equipetrol.json)

add_executable(hardware_test tests/hardware_test.cpp src/hardware.cpp)
target_include_directories(hardware_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(hardware_test PRIVATE -Wall -Wextra)
target_link_libraries(hardware_test PRIVATE Threads::Threads)
apply_sanitizers(hardware_test)
add_test(NAME hardware_test COMMAND hardware_test)

add_executable(stop_signal_test tests/stop_signal_test.cpp src/stop_signal.cpp)
target_include_directories(stop_signal_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(stop_signal_test PRIVATE -Wall -Wextra)
target_link_libraries(stop_signal_test PRIVATE Threads::Threads)
apply_sanitizers(stop_signal_test)
add_test(NAME stop_signal_test COMMAND stop_signal_test)

add_executable(thread_pool_test tests/thread_pool_test.cpp src/thread_pool.cpp)
target_include_directories(thread_pool_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(thread_pool_test PRIVATE -Wall -Wextra)
target_link_libraries(thread_pool_test PRIVATE Threads::Threads)
apply_sanitizers(thread_pool_test)
add_test(NAME thread_pool_test COMMAND thread_pool_test)

# Verificador del log de eventos (Python 3, solo libreria estandar)
add_test(NAME check_log_test COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/check_log_test.py)

# Carga y validacion del config: cada archivo de tests/invalid/ debe rechazarse
# con estado distinto de cero y un mensaje; tests/config/minimal.json debe cargar.
add_test(NAME config_tests
         COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_config_tests.sh $<TARGET_FILE:delivery_sim>)
