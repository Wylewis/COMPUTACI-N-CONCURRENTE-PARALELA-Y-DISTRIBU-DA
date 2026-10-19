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
find_package(OpenCV CONFIG)
if(OpenCV_FOUND)
  message(STATUS "OpenCV ${OpenCV_VERSION}: delivery_sim con ventana")
else()
  message(WARNING "OpenCV no encontrado: delivery_sim se construye sin ventana")
endif()

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
  src/event_logger.cpp
  src/order_book.cpp
  src/generator_thread.cpp
  src/courier.cpp
  src/courier_thread.cpp
  src/dispatch_select.cpp
  src/fleet.cpp
  src/dispatcher_thread.cpp
  src/restaurant.cpp
  src/restaurants.cpp
  src/breakdown.cpp
  src/live_counters.cpp
)

# nlohmann/json (header unico, MIT) vive en third_party/. Se incluye como SYSTEM
# para que sus cabeceras no generen warnings bajo -Wall -Wextra.
target_include_directories(delivery_sim SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

target_compile_options(delivery_sim PRIVATE -Wall -Wextra)
target_link_libraries(delivery_sim PRIVATE Threads::Threads)
if(OpenCV_FOUND)
  target_sources(delivery_sim PRIVATE src/window.cpp src/fleet_view.cpp)
  target_include_directories(delivery_sim SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(delivery_sim PRIVATE opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui)
  target_compile_definitions(delivery_sim PRIVATE HAVE_OPENCV=1)

  add_executable(window_test tests/window_test.cpp src/window.cpp src/map_projection.cpp src/graph.cpp src/config.cpp)
  target_include_directories(window_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_include_directories(window_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party ${OpenCV_INCLUDE_DIRS})
  target_compile_options(window_test PRIVATE -Wall -Wextra)
  target_link_libraries(window_test PRIVATE opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui)
  apply_sanitizers(window_test)
  add_test(NAME window_test COMMAND window_test ${CMAKE_SOURCE_DIR}/config/equipetrol.json)

  add_executable(fleet_view_test tests/fleet_view_test.cpp src/fleet_view.cpp src/courier.cpp src/order.cpp
    src/route_motion.cpp src/map_projection.cpp src/graph.cpp src/config.cpp)
  target_include_directories(fleet_view_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_include_directories(fleet_view_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party ${OpenCV_INCLUDE_DIRS})
  target_compile_options(fleet_view_test PRIVATE -Wall -Wextra)
  target_link_libraries(fleet_view_test PRIVATE opencv_core opencv_imgproc)
  apply_sanitizers(fleet_view_test)
  add_test(NAME fleet_view_test COMMAND fleet_view_test)
endif()
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

add_executable(live_counters_test tests/live_counters_test.cpp src/live_counters.cpp)
target_include_directories(live_counters_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(live_counters_test PRIVATE -Wall -Wextra)
target_link_libraries(live_counters_test PRIVATE Threads::Threads)
apply_sanitizers(live_counters_test)
add_test(NAME live_counters_test COMMAND live_counters_test)

add_executable(breakdown_test tests/breakdown_test.cpp src/breakdown.cpp)
target_include_directories(breakdown_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(breakdown_test PRIVATE -Wall -Wextra)
apply_sanitizers(breakdown_test)
add_test(NAME breakdown_test COMMAND breakdown_test)

add_executable(restaurant_test tests/restaurant_test.cpp src/restaurant.cpp src/stop_signal.cpp src/config.cpp)
target_include_directories(restaurant_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(restaurant_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(restaurant_test PRIVATE -Wall -Wextra)
target_link_libraries(restaurant_test PRIVATE Threads::Threads)
apply_sanitizers(restaurant_test)
add_test(NAME restaurant_test COMMAND restaurant_test)

add_executable(fleet_test tests/fleet_test.cpp
  src/fleet.cpp src/dispatch_select.cpp src/thread_pool.cpp src/estimate.cpp src/courier_thread.cpp src/breakdown.cpp src/live_counters.cpp src/restaurants.cpp src/restaurant.cpp src/courier.cpp src/route_motion.cpp src/graph.cpp src/order.cpp
  src/config.cpp src/event_logger.cpp src/event_log.cpp src/sim_clock.cpp src/stop_signal.cpp)
target_include_directories(fleet_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(fleet_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(fleet_test PRIVATE -Wall -Wextra)
target_link_libraries(fleet_test PRIVATE Threads::Threads)
apply_sanitizers(fleet_test)
add_test(NAME fleet_test COMMAND fleet_test)

add_executable(breakdown_stress_test tests/breakdown_stress_test.cpp
  src/dispatcher_thread.cpp src/fleet.cpp src/dispatch_select.cpp src/thread_pool.cpp src/courier_thread.cpp src/breakdown.cpp src/live_counters.cpp src/restaurants.cpp src/restaurant.cpp src/courier.cpp
  src/order_book.cpp src/estimate.cpp src/route_motion.cpp src/graph.cpp src/order.cpp src/config.cpp
  src/event_logger.cpp src/event_log.cpp src/sim_clock.cpp src/stop_signal.cpp)
target_include_directories(breakdown_stress_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(breakdown_stress_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(breakdown_stress_test PRIVATE -Wall -Wextra)
target_link_libraries(breakdown_stress_test PRIVATE Threads::Threads)
apply_sanitizers(breakdown_stress_test)
add_test(NAME stress_test COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_stress_test.sh $<TARGET_FILE:breakdown_stress_test>)

add_executable(dispatcher_thread_test tests/dispatcher_thread_test.cpp
  src/dispatcher_thread.cpp src/fleet.cpp src/dispatch_select.cpp src/thread_pool.cpp src/courier_thread.cpp src/breakdown.cpp src/live_counters.cpp src/restaurants.cpp src/restaurant.cpp src/courier.cpp
  src/order_book.cpp src/estimate.cpp src/route_motion.cpp src/graph.cpp src/order.cpp src/config.cpp
  src/event_logger.cpp src/event_log.cpp src/sim_clock.cpp src/stop_signal.cpp)
target_include_directories(dispatcher_thread_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(dispatcher_thread_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(dispatcher_thread_test PRIVATE -Wall -Wextra)
target_link_libraries(dispatcher_thread_test PRIVATE Threads::Threads)
apply_sanitizers(dispatcher_thread_test)
add_test(NAME dispatcher_thread_test COMMAND dispatcher_thread_test)

add_executable(dispatch_select_test tests/dispatch_select_test.cpp
  src/dispatch_select.cpp src/thread_pool.cpp src/estimate.cpp src/route_motion.cpp src/graph.cpp src/order.cpp src/config.cpp)
target_include_directories(dispatch_select_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(dispatch_select_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(dispatch_select_test PRIVATE -Wall -Wextra)
target_link_libraries(dispatch_select_test PRIVATE Threads::Threads)
apply_sanitizers(dispatch_select_test)
add_test(NAME dispatch_select_test COMMAND dispatch_select_test)

add_executable(courier_thread_test tests/courier_thread_test.cpp
  src/courier_thread.cpp src/breakdown.cpp src/live_counters.cpp src/restaurants.cpp src/restaurant.cpp src/courier.cpp src/order.cpp src/route_motion.cpp src/graph.cpp src/config.cpp
  src/event_logger.cpp src/event_log.cpp src/sim_clock.cpp src/stop_signal.cpp)
target_include_directories(courier_thread_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(courier_thread_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(courier_thread_test PRIVATE -Wall -Wextra)
target_link_libraries(courier_thread_test PRIVATE Threads::Threads)
apply_sanitizers(courier_thread_test)
add_test(NAME courier_thread_test COMMAND courier_thread_test)

add_executable(courier_test tests/courier_test.cpp
  src/courier.cpp src/order.cpp src/route_motion.cpp src/graph.cpp src/config.cpp)
target_include_directories(courier_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(courier_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(courier_test PRIVATE -Wall -Wextra)
target_link_libraries(courier_test PRIVATE Threads::Threads)
apply_sanitizers(courier_test)
add_test(NAME courier_test COMMAND courier_test)

add_executable(order_book_test tests/order_book_test.cpp src/order_book.cpp src/order.cpp)
target_include_directories(order_book_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(order_book_test PRIVATE -Wall -Wextra)
target_link_libraries(order_book_test PRIVATE Threads::Threads)
apply_sanitizers(order_book_test)
add_test(NAME order_book_test COMMAND order_book_test)

add_executable(event_logger_test tests/event_logger_test.cpp src/event_logger.cpp src/event_log.cpp src/order.cpp)
target_include_directories(event_logger_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(event_logger_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_options(event_logger_test PRIVATE -Wall -Wextra)
target_link_libraries(event_logger_test PRIVATE Threads::Threads)
apply_sanitizers(event_logger_test)
add_test(NAME event_logger_test COMMAND event_logger_test)

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

# Integracion: corridas completas por tiempo cumplido, SIGINT y SIGTERM,
# verificadas con el verificador del log.
add_test(NAME sim_tests COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_sim_tests.sh $<TARGET_FILE:delivery_sim>)

# Verificador del log de eventos (Python 3, solo libreria estandar)
add_test(NAME check_log_test COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/check_log_test.py)

# Carga y validacion del config: cada archivo de tests/invalid/ debe rechazarse
# con estado distinto de cero y un mensaje; tests/config/minimal.json debe cargar.
add_test(NAME config_tests
         COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_config_tests.sh $<TARGET_FILE:delivery_sim>)
