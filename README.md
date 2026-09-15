# SANKET Signal Analysis Pipeline (SIH26147)

A high-performance, real-time GUI dashboard and DSP pipeline for visualizing raw RF signals. It features an FFT Waterfall and an I/Q Constellation plot, optimized for massive files using zero-copy memory mapping (`mmap()`) and AVX2 SIMD intrinsics.

## Features
- **Zero-Copy Ingestion**: Opens massive (50GB+) files instantly with zero RAM overhead.
- **Dual Analysis Pipeline**: Simultaneous Time-domain (Constellation) and Frequency-domain (Waterfall) processing.
- **Hardware Acceleration**: Multi-threaded DSP backend (`std::async`) and 256-bit AVX2 SIMD vectorization.
- **Interactive Playback**: Live scrolling, history jumping, and pause capabilities.
- **Automatic Signal Generation**: Includes a GNU Radio C++ binding to generate synthetic raw `.iq` and `.wav` test files.

## Environment Setup (Ubuntu / WSL2)

The project requires a modern C++20 compiler, Qt6 for the GUI, FFTW3 for DSP math, and GNU Radio for generating synthetic test signals.

### 1. Install Dependencies
Run the following in your Ubuntu/WSL terminal to install all required libraries:

```bash
sudo apt update
sudo apt install -y build-essential cmake git
sudo apt install -y qt6-base-dev libqt6charts6-dev
sudo apt install -y libfftw3-dev
sudo apt install -y gnuradio-dev  <--- GNU Radio C++ Development Libraries
```

*(Note for Windows 10/11 WSL users: Ensure you have WSLg or an X11 server like VcXsrv installed to render the Qt6 GUI from your Linux terminal).*

### 2. Build the Project
We use CMake to build the project. From the root of the project directory (`SIH 2026`), execute:

```bash
mkdir build
cd build
cmake ..
cmake --build . -j$(nproc)
```

## Running the Dashboard (Stage 4 - Final GUI)

Once built, launch the GUI directly from the build directory:

```bash
./waterfall_stage3
```

1. Click **Upload Signal File**.
2. Navigate to your signal file (`.iq` or `.wav`).
3. Click **Start Analysis** to seamlessly load and visualize the data.

## Running the CLI Waterfall (Stage 1 - Sanity Check)

If you wish to test the underlying DSP core without the Qt6 GUI, you can run the Stage 1 CLI tool which dumps the waterfall matrix as a static `.ppm` image.

```bash
# from the build/ directory, run the tool and point it at your own files:
./waterfall_stage1 path/to/signal.wav output/mine.ppm
```

Open `output/mine.ppm` in any image viewer, or convert it to PNG:
```bash
convert output/mine.ppm output/mine.png
```

### Stage 1 Notes & Gotchas:
- The FFTW plan is created once and reused across all windows in `compute_waterfall()` — creating a new plan per FFT call is a common performance mistake, avoid reintroducing it later.
- `hop_size < fft_size` gives overlapping windows (smoother waterfall); `hop_size == fft_size` gives no overlap. The default (1024/256) is 75% overlap.
- Magnitude is converted to dB (`20*log10`) before display — raw linear magnitude is visually unreadable for real signals.
- `write_ppm` is a placeholder for Stage 1 only. It normalizes per-image, so absolute magnitude isn't comparable across different runs/files. The Stage 4 GUI uses a fixed/user-adjustable color scale instead.

## Generating Synthetic Signals (GNU Radio)

If you don't have a massive test signal on hand, this repository includes a pure C++ GNU Radio generator that synthesizes a pure Continuous Wave (CW) sine signal directly to your disk.

To generate a test file (e.g., a massive 20GB IQ file):
```bash
# Usage: ./generate_gnuradio_signal <duration_seconds> <sample_rate_hz> <output_iq> <output_wav>

./build/generate_gnuradio_signal 2684.35 1000000 ./massive_20gb.iq ./massive_10gb.wav
```
This will generate the synthetic files in the current directory.
