cmake_minimum_required(VERSION 3.16)

project(audio_client_sg2002_linux LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ESP32_APP_DIR "${CMAKE_CURRENT_LIST_DIR}/../audio-client-esp32s3")
set(ESP32_SRC_DIR "${ESP32_APP_DIR}/src")
set(TFLM_DIR "${ESP32_APP_DIR}/managed_components/espressif__esp-tflite-micro")
set(MICRO_FRONTEND_DIR "${ESP32_SRC_DIR}/tensorflow/lite/experimental/microfrontend/lib")

file(GLOB MICRO_FRONTEND_SOURCES CONFIGURE_DEPENDS
  "${MICRO_FRONTEND_DIR}/*.c"
  "${MICRO_FRONTEND_DIR}/*.cc"
  "${MICRO_FRONTEND_DIR}/*.cpp")

file(GLOB TFLM_KERNEL_SOURCES CONFIGURE_DEPENDS
  "${TFLM_DIR}/tensorflow/lite/micro/kernels/*.c"
  "${TFLM_DIR}/tensorflow/lite/micro/kernels/*.cc")

file(GLOB TFLM_BRIDGE_SOURCES CONFIGURE_DEPENDS
  "${TFLM_DIR}/tensorflow/lite/micro/tflite_bridge/*.c"
  "${TFLM_DIR}/tensorflow/lite/micro/tflite_bridge/*.cc")

set(TFLM_CORE_SOURCES
  "${TFLM_DIR}/tensorflow/lite/micro/debug_log.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/flatbuffer_utils.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/memory_helpers.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_allocation_info.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_allocator.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_context.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_interpreter_context.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_interpreter_graph.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_interpreter.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_log.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_op_resolver.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_profiler.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_resource_variable.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_time.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/micro_utils.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/recording_micro_allocator.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/system_setup.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/memory_planner/greedy_memory_planner.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/memory_planner/linear_memory_planner.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/arena_allocator/non_persistent_arena_buffer_allocator.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/arena_allocator/persistent_arena_buffer_allocator.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/arena_allocator/recording_single_arena_buffer_allocator.cc"
  "${TFLM_DIR}/tensorflow/lite/micro/arena_allocator/single_arena_buffer_allocator.cc"
  "${TFLM_DIR}/tensorflow/lite/core/c/common.cc"
  "${TFLM_DIR}/tensorflow/lite/core/api/flatbuffer_conversions.cc"
  "${TFLM_DIR}/tensorflow/lite/core/api/tensor_utils.cc"
  "${TFLM_DIR}/tensorflow/lite/kernels/kernel_util.cc"
  "${TFLM_DIR}/tensorflow/lite/kernels/internal/common.cc"
  "${TFLM_DIR}/tensorflow/lite/kernels/internal/quantization_util.cc"
  "${TFLM_DIR}/tensorflow/lite/kernels/internal/portable_tensor_utils.cc"
  "${TFLM_DIR}/tensorflow/lite/kernels/internal/tensor_utils.cc"
  "${TFLM_DIR}/tensorflow/lite/kernels/internal/tensor_ctypes.cc"
  "${TFLM_DIR}/tensorflow/lite/kernels/internal/reference/portable_tensor_utils.cc"
  "${TFLM_DIR}/tensorflow/lite/kernels/internal/reference/comparisons.cc"
  "${TFLM_DIR}/tensorflow/compiler/mlir/lite/core/api/error_reporter.cc"
  "${TFLM_DIR}/tensorflow/compiler/mlir/lite/schema/schema_utils.cc")

add_executable(audio_wakeword_linux
  main.cpp
  "${ESP32_SRC_DIR}/wakeword.cc"
  ${MICRO_FRONTEND_SOURCES}
  ${TFLM_KERNEL_SOURCES}
  ${TFLM_BRIDGE_SOURCES}
  ${TFLM_CORE_SOURCES})

target_include_directories(audio_wakeword_linux PRIVATE
  "${ESP32_SRC_DIR}"
  "${MICRO_FRONTEND_DIR}"
  "${TFLM_DIR}"
  "${TFLM_DIR}/third_party/gemmlowp"
  "${TFLM_DIR}/third_party/flatbuffers/include"
  "${TFLM_DIR}/third_party/ruy"
  "${TFLM_DIR}/third_party/kissfft"
  "${TFLM_DIR}/tensorflow/lite"
  "${TFLM_DIR}/tensorflow/compiler/mlir")

target_compile_definitions(audio_wakeword_linux PRIVATE
  TF_LITE_STATIC_MEMORY
  TF_LITE_DISABLE_X86_NEON)

target_compile_options(audio_wakeword_linux PRIVATE
  -O3
  -Wall
  -Wextra
  -Wno-sign-compare
  -Wno-unused-parameter
  -Wno-missing-field-initializers
  -ffunction-sections
  -fdata-sections
  -fno-exceptions
  -fno-rtti
  -fno-threadsafe-statics)

if(CMAKE_CROSSCOMPILING)
  target_link_options(audio_wakeword_linux PRIVATE -Wl,--gc-sections -static)
else()
  target_link_options(audio_wakeword_linux PRIVATE -Wl,--gc-sections)
endif()

target_link_libraries(audio_wakeword_linux PRIVATE m)

# Pipe version (reads raw PCM from stdin, use with: arecord ... | ./audio_wakeword_pipe)
add_executable(audio_wakeword_pipe
  main_pipe.cpp
  "${ESP32_SRC_DIR}/wakeword.cc"
  ${MICRO_FRONTEND_SOURCES}
  ${TFLM_KERNEL_SOURCES}
  ${TFLM_BRIDGE_SOURCES}
  ${TFLM_CORE_SOURCES})

target_include_directories(audio_wakeword_pipe PRIVATE
  "${ESP32_SRC_DIR}"
  "${MICRO_FRONTEND_DIR}"
  "${TFLM_DIR}"
  "${TFLM_DIR}/third_party/gemmlowp"
  "${TFLM_DIR}/third_party/flatbuffers/include"
  "${TFLM_DIR}/third_party/ruy"
  "${TFLM_DIR}/third_party/kissfft"
  "${TFLM_DIR}/tensorflow/lite"
  "${TFLM_DIR}/tensorflow/compiler/mlir")

target_compile_definitions(audio_wakeword_pipe PRIVATE
  TF_LITE_STATIC_MEMORY
  TF_LITE_DISABLE_X86_NEON)

target_compile_options(audio_wakeword_pipe PRIVATE
  -O3 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter
  -Wno-missing-field-initializers -ffunction-sections -fdata-sections
  -fno-exceptions -fno-rtti -fno-threadsafe-statics)

if(CMAKE_CROSSCOMPILING)
  target_link_options(audio_wakeword_pipe PRIVATE -Wl,--gc-sections -static)
else()
  target_link_options(audio_wakeword_pipe PRIVATE -Wl,--gc-sections)
endif()

target_link_libraries(audio_wakeword_pipe PRIVATE m)

# ALSA version (I2S capture from INMP441) - optional, requires libasound2-dev
find_package(ALSA QUIET)

if(ALSA_FOUND)
  add_executable(audio_wakeword_alsa
    main_alsa.cpp
    "${ESP32_SRC_DIR}/wakeword.cc"
    ${MICRO_FRONTEND_SOURCES}
    ${TFLM_KERNEL_SOURCES}
    ${TFLM_BRIDGE_SOURCES}
    ${TFLM_CORE_SOURCES})

  target_include_directories(audio_wakeword_alsa PRIVATE
    "${ESP32_SRC_DIR}"
    "${MICRO_FRONTEND_DIR}"
    "${TFLM_DIR}"
    "${TFLM_DIR}/third_party/gemmlowp"
    "${TFLM_DIR}/third_party/flatbuffers/include"
    "${TFLM_DIR}/third_party/ruy"
    "${TFLM_DIR}/third_party/kissfft"
    "${TFLM_DIR}/tensorflow/lite"
    "${TFLM_DIR}/tensorflow/compiler/mlir"
    ${ALSA_INCLUDE_DIRS})

  target_compile_definitions(audio_wakeword_alsa PRIVATE
    TF_LITE_STATIC_MEMORY
    TF_LITE_DISABLE_X86_NEON)

  target_compile_options(audio_wakeword_alsa PRIVATE
    -O3
    -Wall
    -Wextra
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -ffunction-sections
    -fdata-sections
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics)

  target_link_options(audio_wakeword_alsa PRIVATE -Wl,--gc-sections)

  target_link_libraries(audio_wakeword_alsa PRIVATE m ${ALSA_LIBRARIES})
  
  message(STATUS "ALSA found - building audio_wakeword_alsa target")
else()
  message(STATUS "ALSA not found - skipping audio_wakeword_alsa target")
  message(STATUS "To enable: install libasound2-dev (Ubuntu/Debian)")
endif()
