cmake_minimum_required(VERSION 3.16)
project(visualization CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependencies ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW REQUIRED fftw3f)

# Automatically find Qt6 components, with MOC enabled
set(CMAKE_AUTOMOC ON)
find_package(Qt6 COMPONENTS Core Gui Widgets OpenGLWidgets REQUIRED)

# Stage 1: Waterfall PPM Dumper
add_executable(waterfall_stage1
    src/main.cpp
    src/waterfall.cpp
    src/IQReader.cpp
    src/WAVReader.cpp
    src/MemoryMappedFile.cpp
)

target_include_directories(waterfall_stage1 PRIVATE
    ${FFTW_INCLUDE_DIRS}
    src
    ../include
)

target_link_libraries(waterfall_stage1 PRIVATE
    ${FFTW_LIBRARIES}
)

target_compile_options(waterfall_stage1 PRIVATE ${FFTW_CFLAGS_OTHER} -mavx -mavx2)

# Stage 2: Constellation Qt GUI Test
add_executable(constellation_gui_test
    src/main_qt_test.cpp
    src/ConstellationPlotWidget.cpp
    src/FeatureExtractor.cpp
)
target_include_directories(constellation_gui_test PRIVATE src)
target_link_libraries(constellation_gui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets)
target_compile_options(constellation_gui_test PRIVATE -mavx -mavx2)

# Stage 3: Unified Real-Time GUI (Waterfall + Constellation)
add_executable(waterfall_stage3
    src/main_qt.cpp
    src/waterfall.cpp
    src/IQReader.cpp
    src/WAVReader.cpp
    src/MemoryMappedFile.cpp
    src/ConstellationPlotWidget.cpp
    src/WaterfallPlotWidget.cpp
    src/SpectrumPlotWidget.cpp
    src/TimePlotWidget.cpp
    src/FeatureExtractor.cpp
)

target_include_directories(waterfall_stage3 PRIVATE
    ${FFTW_INCLUDE_DIRS}
    src
    ../include
)

target_link_libraries(waterfall_stage3 PRIVATE
    ${FFTW_LIBRARIES}
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets
)

target_compile_options(waterfall_stage3 PRIVATE ${FFTW_CFLAGS_OTHER} -mavx -mavx2)
