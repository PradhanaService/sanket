#include <QApplication>
#include <QMainWindow>
#include <thread>
#include <chrono>
#include <random>
#include "ConstellationPlotWidget.hpp"
#include "ConstellationDataBuffer.hpp"
#include "FeatureExtractor.hpp"

// Synthetic Signal Generator Thread
// This simulates the C++ DSP background thread feeding the UI pipeline
void mockDSPThread(std::shared_ptr<ConstellationDataBuffer> buffer, bool& isRunning) {
    std::mt19937 rng(std::random_device{}());
    // Create standard normal distribution for AWGN (Additive White Gaussian Noise)
    std::normal_distribution<float> noise(0.0f, 0.08f); 

    std::vector<std::complex<float>> frame;
    frame.resize(4096);

    // Define 16-QAM constellation ideal mapping points
    const float qam_levels[4] = {-1.0f, -0.33f, 0.33f, 1.0f};

    while (isRunning) {
        for (size_t i = 0; i < frame.size(); ++i) {
            // Pick a random ideal 16-QAM discrete point
            float ideal_i = qam_levels[rng() % 4];
            float ideal_q = qam_levels[rng() % 4];

            // Add AWGN noise to simulate a real-world imperfect channel transmission
            float noisy_i = ideal_i + noise(rng);
            float noisy_q = ideal_q + noise(rng);

            frame[i] = std::complex<float>(noisy_i, noisy_q);
        }

        // Push full frame to thread-safe lock-free boundary
        buffer->push(frame);

        // Simulate high-speed radio ingestion (e.g. 50 chunks per second)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    auto dataBuffer = std::make_shared<ConstellationDataBuffer>(8192);
    auto sharedFeat = std::make_shared<SharedFeatures>(); // Mock features for testing
    sharedFeat->data.modulation_type = "16-QAM (Simulated)";
    sharedFeat->data.estimated_evm = 8.5f;

    QMainWindow mainWindow;
    mainWindow.resize(800, 800);
    mainWindow.setWindowTitle("NTRO Constellation Plotter (16-QAM Synthetic Test)");

    auto plotWidget = new ConstellationPlotWidget(dataBuffer, sharedFeat, &mainWindow);
    mainWindow.setCentralWidget(plotWidget);

    // Launch Background DSP Thread
    bool isRunning = true;
    std::thread dspWorker(mockDSPThread, dataBuffer, std::ref(isRunning));

    mainWindow.show();
    
    // Enter the Qt Main GUI Event Loop (Blocks here until window is closed)
    int ret = app.exec();

    // Clean up background thread safely
    isRunning = false;
    if (dspWorker.joinable()) {
        dspWorker.join();
    }

    return ret;
}
