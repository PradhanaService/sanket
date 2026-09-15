cmake_minimum_required(VERSION 3.10)
project(SIH_Data_Ingestion CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND MINGW)
    add_compile_options(
        "-isystemC:/PROGRA~2/Dev-Cpp/MinGW64/lib/gcc/x86_64-w64-mingw32/4.9.2/include/c++"
        "-isystemC:/PROGRA~2/Dev-Cpp/MinGW64/lib/gcc/x86_64-w64-mingw32/4.9.2/include/c++/x86_64-w64-mingw32"
        "-isystemC:/PROGRA~2/Dev-Cpp/MinGW64/x86_64-w64-mingw32/include"
    )
endif()

# Add platform-aware compiler flags for SIMD AVX2 and threading
if(MSVC)
    add_compile_options(/W4 /arch:AVX2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -mavx2 -pthread)
endif()

# Include directories
include_directories(include)

# Create a library for the ingestion components to easily link them to tests and main app
add_library(signal_ingestion_lib
    src/MemoryMappedFile.cpp
    src/DataIngestor.cpp
    src/IQReader.cpp
    src/WAVReader.cpp
    src/StreamIngestor.cpp
    src/SpectrumAnalyzer.cpp
)

# Find FFTW3 (Directly find the library without relying on pkg-config)
find_library(FFTW3F_LIBRARY NAMES fftw3f libfftw3f-3 fftw3f-3 fftw3)
find_path(FFTW3_INCLUDE_DIR NAMES fftw3.h)

if(FFTW3F_LIBRARY AND FFTW3_INCLUDE_DIR)
    message(STATUS "Found FFTW3 library: ${FFTW3F_LIBRARY}")
    target_link_libraries(signal_ingestion_lib PUBLIC ${FFTW3F_LIBRARY})
    target_include_directories(signal_ingestion_lib PUBLIC ${FFTW3_INCLUDE_DIR})
else()
    message(STATUS "FFTW3 single-precision library not found natively. Enabling built-in SIMD FFT engine fallback.")
    target_compile_definitions(signal_ingestion_lib PUBLIC NO_FFTW3)
endif()

if(UNIX OR MINGW)
    find_library(MATH_LIB m)
    if(MATH_LIB)
        target_link_libraries(signal_ingestion_lib PUBLIC ${MATH_LIB})
    endif()
endif()

if(MINGW)
    find_library(WINPTHREAD_LIB winpthread)
    if(WINPTHREAD_LIB)
        target_link_libraries(signal_ingestion_lib PUBLIC ${WINPTHREAD_LIB})
    endif()
endif()

# Main Application
add_executable(signal_app src/main.cpp)
target_link_libraries(signal_app PRIVATE signal_ingestion_lib)

# C++ Signal Verification Utility
add_executable(check_signal src/check_signal.cpp)
target_link_libraries(check_signal PRIVATE signal_ingestion_lib)

# GNU Radio C++ Signal Generator
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GNURADIO gnuradio-runtime gnuradio-analog gnuradio-blocks)
endif()

if(GNURADIO_FOUND)
    find_library(FMT_LIB NAMES fmt)
    add_executable(generate_gnuradio_signal src/generate_gnuradio_signal.cpp)
    target_include_directories(generate_gnuradio_signal PRIVATE ${GNURADIO_INCLUDE_DIRS})
    target_link_libraries(generate_gnuradio_signal PRIVATE ${GNURADIO_LIBRARIES} ${FMT_LIB})
else()
    find_library(GNURADIO_RUNTIME_LIB NAMES gnuradio-runtime)
    find_library(GNURADIO_ANALOG_LIB NAMES gnuradio-analog)
    find_library(GNURADIO_BLOCKS_LIB NAMES gnuradio-blocks)
    find_library(FMT_LIB NAMES fmt)
    if(GNURADIO_RUNTIME_LIB AND GNURADIO_ANALOG_LIB AND GNURADIO_BLOCKS_LIB)
        add_executable(generate_gnuradio_signal src/generate_gnuradio_signal.cpp)
        target_link_libraries(generate_gnuradio_signal PRIVATE ${GNURADIO_RUNTIME_LIB} ${GNURADIO_ANALOG_LIB} ${GNURADIO_BLOCKS_LIB} ${FMT_LIB})
    endif()
endif()

# Automated Tests (only enabled if tests directory exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()

    add_executable(test_iq tests/test_iq.cpp)
    target_link_libraries(test_iq PRIVATE signal_ingestion_lib)
    add_test(NAME TestIQ COMMAND test_iq)

    add_executable(test_wav tests/test_wav.cpp)
    target_link_libraries(test_wav PRIVATE signal_ingestion_lib)
    add_test(NAME TestWAV COMMAND test_wav)

    add_executable(test_invalid tests/test_invalid.cpp)
    target_link_libraries(test_invalid PRIVATE signal_ingestion_lib)
    add_test(NAME TestInvalid COMMAND test_invalid)

    add_executable(test_data_ingestor_iq tests/test_data_ingestor_iq.cpp)
    target_link_libraries(test_data_ingestor_iq PRIVATE signal_ingestion_lib)
    add_test(NAME TestDataIngestorIQ COMMAND test_data_ingestor_iq)

    add_executable(test_data_ingestor_wav tests/test_data_ingestor_wav.cpp)
    target_link_libraries(test_data_ingestor_wav PRIVATE signal_ingestion_lib)
    add_test(NAME TestDataIngestorWAV COMMAND test_data_ingestor_wav)

    add_executable(test_unsupported_format tests/test_unsupported_format.cpp)
    target_link_libraries(test_unsupported_format PRIVATE signal_ingestion_lib)
    add_test(NAME TestUnsupportedFormat COMMAND test_unsupported_format)

    add_executable(test_ping_pong_streaming tests/test_ping_pong_streaming.cpp)
    target_link_libraries(test_ping_pong_streaming PRIVATE signal_ingestion_lib)
    add_test(NAME TestPingPongStreaming COMMAND test_ping_pong_streaming)
endif()
