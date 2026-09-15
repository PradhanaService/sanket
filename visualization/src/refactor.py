import os
import re

cpp_code = """
#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QGridLayout>
#include <QWidget>
#include <thread>
#include <chrono>
#include <iostream>
#include <QTimer>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <mutex>
#include <QDialog>
#include <QTextBrowser>
#include <QSlider>
#include <QPushButton>
#include <QFileDialog>
#include <filesystem>
#include <atomic>
#include <cmath>

#include "SignalData.hpp"
#include "IQReader.hpp"
#include "WAVReader.hpp"
#include "waterfall.h"

#include "ConstellationPlotWidget.hpp"
#include "WaterfallPlotWidget.hpp"
#include "ConstellationDataBuffer.hpp"
#include "WaterfallDataBuffer.hpp"
#include "FeatureExtractor.hpp"

bool ends_with(const std::string& str, const std::string& suffix) {
    if (str.length() >= suffix.length()) {
        return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
    }
    return false;
}

QString formatBytes(uint64_t bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

void dspPlaybackThread(
    SignalData data,
    std::shared_ptr<ConstellationDataBuffer> constelBuffer,
    std::shared_ptr<WaterfallDataBuffer> waterBuffer,
    std::shared_ptr<SharedFeatures> sharedFeat,
    std::atomic<bool>& isRunning) 
{
    FeatureExtractor extractor;
    WaterfallParams params;
    params.fft_size = 1024;
    params.hop_size = 256;
    
    double default_sample_rate = data.sample_rate > 0 ? data.sample_rate : 44100.0;
    int sleep_ms = static_cast<int>((params.hop_size / default_sample_rate) * 1000.0);
    if (sleep_ms < 1) sleep_ms = 1;

    size_t start = 0;
    size_t row_idx = 0;
    
    bool has_mmap = (data.mapped_file != nullptr);
    uint64_t m_addr = 0;
    uint64_t m_size = 0;
    if (has_mmap) {
        m_addr = reinterpret_cast<uint64_t>(data.mapped_file->data());
        m_size = data.mapped_file->file_size();
    }
    
    std::cout << "[DEBUG] Thread started. has_mmap=" << has_mmap << ", samples.size()=" << data.samples.size() << std::endl;
    WaterfallProcessor processor(params);
    std::cout << "[DEBUG] Processor created." << std::endl;
    
    auto t_start = std::chrono::steady_clock::now();

    std::cout << "[DEBUG] Loop condition met. Entering while loop..." << std::endl;
    while (isRunning && start + params.fft_size <= data.sample_count) {
        auto t_frame_start = std::chrono::steady_clock::now();
        if (start == 0) std::cout << "[DEBUG] First iteration inside loop!" << std::endl;

        std::vector<std::complex<float>> chunk_buf(params.fft_size);
        if (data.raw_wav_data != nullptr) {
            const uint8_t* curr_ptr = data.raw_wav_data + (start * data.wav_bytes_per_sample * data.wav_num_channels);
            for (size_t i = 0; i < params.fft_size; ++i) {
                float I = 0.0f, Q = 0.0f;
                if (data.wav_audio_format == 1 && data.wav_bytes_per_sample == 2) { 
                    const int16_t* pcm_samples = reinterpret_cast<const int16_t*>(curr_ptr);
                    I = static_cast<float>(pcm_samples[0]) / 32768.0f;
                    if (data.wav_num_channels == 2) Q = static_cast<float>(pcm_samples[1]) / 32768.0f;
                } else if (data.wav_audio_format == 3 && data.wav_bytes_per_sample == 4) { 
                    const float* float_samples = reinterpret_cast<const float*>(curr_ptr);
                    I = float_samples[0];
                    if (data.wav_num_channels == 2) Q = float_samples[1];
                }
                curr_ptr += (data.wav_bytes_per_sample * data.wav_num_channels);
                chunk_buf[i] = std::complex<float>(I, Q);
            }
        } else {
            for (size_t i = 0; i < params.fft_size; ++i) {
                chunk_buf[i] = data.samples[start + i];
            }
        }
        
        std::span<const std::complex<float>> chunk(chunk_buf.data(), params.fft_size);
        std::vector<float> row = processor.process_chunk(chunk);
        waterBuffer->push(row);

        constelBuffer->push(chunk_buf);

        auto current_features = extractor.extract(chunk_buf, row);
        current_features.center_freq = data.center_frequency > 0 ? data.center_frequency : 2.4e9f;
        current_features.sample_rate = data.sample_rate > 0 ? data.sample_rate : 2.048e6f;
        
        auto t_frame_end = std::chrono::steady_clock::now();
        float latency = std::chrono::duration<float, std::milli>(t_frame_end - t_frame_start).count();
        float elapsed_sec = std::chrono::duration<float>(t_frame_end - t_start).count();
        float msps = (elapsed_sec > 0) ? (start / 1e6f) / elapsed_sec : 0.0f;

        {
            std::lock_guard<std::mutex> lock(sharedFeat->mtx);
            sharedFeat->data = current_features;
            sharedFeat->metrics.mmap_active = has_mmap;
            sharedFeat->metrics.mapped_address = m_addr;
            sharedFeat->metrics.mapped_size = m_size;
            sharedFeat->metrics.samples_received = data.sample_count;
            sharedFeat->metrics.samples_processed = start;
            sharedFeat->metrics.data_rate_msps = msps;
            sharedFeat->metrics.processing_latency_ms = latency;
            sharedFeat->metrics.file_name = data.file_name;
            sharedFeat->metrics.source_format = data.source_format;
        }

        start += params.hop_size;
        row_idx++;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}

QLabel* createLabel(const QString& text, const QString& color, bool bold = false, int size = 12, const QString& tooltip = "") {
    QLabel* l = new QLabel(text);
    QString style = QString("color: %1; font-size: %2px;").arg(color).arg(size);
    if (bold) style += " font-weight: bold;";
    l->setStyleSheet(style);
    if (!tooltip.isEmpty()) l->setToolTip(tooltip);
    return l;
}

class DashboardApp : public QMainWindow {
    Q_OBJECT
public:
    DashboardApp(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("NTRO Unified Stage 4 \xe2\x80\x93 Signal Analysis Dashboard");
        resize(1920, 1080);
        setStyleSheet("background-color: #050505; color: #FFFFFF; font-family: 'Segoe UI', Arial;");

        m_stackedWidget = new QStackedWidget(this);
        setCentralWidget(m_stackedWidget);

        initSplashScreen();
        initUploadScreen();
        initDashboardScreen();

        m_stackedWidget->setCurrentIndex(0);

        QTimer::singleShot(1500, this, [this]() {
            m_stackedWidget->setCurrentIndex(1);
        });
        
        m_uiTimer = new QTimer(this);
        connect(m_uiTimer, &QTimer::timeout, this, &DashboardApp::updateDashboard);
    }

    ~DashboardApp() {
        stopAnalysis();
    }

private:
    QStackedWidget* m_stackedWidget;
    
    // Screens
    QWidget* m_splashScreen;
    QWidget* m_uploadScreen;
    QWidget* m_dashboardScreen;

    // Upload Screen UI
    QLabel* m_lblUploadInfo;
    QPushButton* m_btnStartAnalysis;
    QString m_selectedFilePath;

    // Dashboard Screen UI
    struct DashboardUI {
        QLabel* stData;
        QLabel* stMmap;
        QLabel* stBuffer;
        QLabel* stDSP;
        QLabel* stWater;
        QLabel* stIQ;
        
        QLabel* lblFile;
        QLabel* lblFormat;
        QLabel* lblSRate;
        QLabel* lblCenter;
        QLabel* lblBW;
        
        QLabel* hlthPower;
        QLabel* hlthNoise;
        QLabel* hlthSNR;
        QLabel* hlthSNRRatio;
        QLabel* hlthQuality;
        QLabel* hlthInterf;
        QLabel* hlthStab;
        
        QLabel* lblPipeline;
        QLabel* mmapVal1;
        QLabel* mmapVal2;
        QLabel* mmapVal3;
        QLabel* mmapVal5;
        QLabel* mmapVal6;
        
        WaterfallPlotWidget* waterfallWidget;
        ConstellationPlotWidget* constellationWidget;
    } m_ui;

    std::shared_ptr<ConstellationDataBuffer> m_constelBuffer;
    std::shared_ptr<WaterfallDataBuffer> m_waterBuffer;
    std::shared_ptr<SharedFeatures> m_sharedFeat;

    std::thread m_dspWorker;
    std::atomic<bool> m_isRunning{false};
    QTimer* m_uiTimer;

    void initSplashScreen() {
        m_splashScreen = new QWidget();
        QVBoxLayout* layout = new QVBoxLayout(m_splashScreen);
        layout->setAlignment(Qt::AlignCenter);

        QLabel* title = new QLabel("SANKET");
        title->setStyleSheet("font-size: 80px; font-weight: bold; color: #FFFFFF;");
        title->setAlignment(Qt::AlignCenter);

        QLabel* subtitle = new QLabel("Signal Analysis Dashboard");
        subtitle->setStyleSheet("font-size: 24px; color: #AAAAAA;");
        subtitle->setAlignment(Qt::AlignCenter);

        layout->addWidget(title);
        layout->addWidget(subtitle);
        
        m_stackedWidget->addWidget(m_splashScreen);
    }

    void initUploadScreen() {
        m_uploadScreen = new QWidget();
        QVBoxLayout* layout = new QVBoxLayout(m_uploadScreen);
        layout->setAlignment(Qt::AlignCenter);

        QPushButton* btnUpload = new QPushButton("Upload Signal File");
        btnUpload->setFixedSize(300, 60);
        btnUpload->setStyleSheet("QPushButton { background-color: #333333; color: white; border-radius: 5px; font-size: 18px; font-weight: bold; } QPushButton:hover { background-color: #555555; }");

        m_lblUploadInfo = new QLabel("No file selected.");
        m_lblUploadInfo->setStyleSheet("font-size: 16px; color: #AAAAAA; margin-top: 20px;");
        m_lblUploadInfo->setAlignment(Qt::AlignCenter);

        m_btnStartAnalysis = new QPushButton("Start Analysis");
        m_btnStartAnalysis->setFixedSize(300, 60);
        m_btnStartAnalysis->setStyleSheet("QPushButton { background-color: #005500; color: white; border-radius: 5px; font-size: 18px; font-weight: bold; } QPushButton:hover { background-color: #007700; } QPushButton:disabled { background-color: #222222; color: #555555; }");
        m_btnStartAnalysis->setEnabled(false);

        layout->addWidget(btnUpload, 0, Qt::AlignCenter);
        layout->addWidget(m_lblUploadInfo, 0, Qt::AlignCenter);
        layout->addSpacing(30);
        layout->addWidget(m_btnStartAnalysis, 0, Qt::AlignCenter);

        connect(btnUpload, &QPushButton::clicked, this, &DashboardApp::onUploadClicked);
        connect(m_btnStartAnalysis, &QPushButton::clicked, this, &DashboardApp::onStartAnalysisClicked);

        m_stackedWidget->addWidget(m_uploadScreen);
    }

    void onUploadClicked() {
        QString fileName = QFileDialog::getOpenFileName(this, "Select Signal File", "", "Signal Files (*.wav *.iq);;All Files (*)");
        if (!fileName.isEmpty()) {
            m_selectedFilePath = fileName;
            std::string stdPath = fileName.toStdString();
            
            std::error_code ec;
            uint64_t fsize = std::filesystem::file_size(stdPath, ec);
            QString sizeStr = formatBytes(fsize);
            
            QString typeStr = "Unknown";
            if (ends_with(stdPath, ".wav") || ends_with(stdPath, ".WAV")) typeStr = "WAV";
            else if (ends_with(stdPath, ".iq") || ends_with(stdPath, ".IQ")) typeStr = "IQ";

            m_lblUploadInfo->setText(QString("File: %1\\nType: %2\\nSize: %3").arg(fileName).arg(typeStr).arg(sizeStr));
            m_btnStartAnalysis->setEnabled(true);
            m_btnStartAnalysis->setText("Start Analysis");
        }
    }

    void onStartAnalysisClicked() {
        m_btnStartAnalysis->setText("Converting to complex<float>...");
        m_btnStartAnalysis->setEnabled(false);
        QApplication::processEvents(); // Force UI update

        SignalData data;
        std::string stdPath = m_selectedFilePath.toStdString();
        bool loaded = false;

        if (ends_with(stdPath, ".wav") || ends_with(stdPath, ".WAV")) {
            loaded = WAVReader::read(stdPath, data);
        } else if (ends_with(stdPath, ".iq") || ends_with(stdPath, ".IQ")) {
            loaded = IQReader::read(stdPath, data);
        }

        if (!loaded) {
            m_lblUploadInfo->setText("Failed to load file!");
            return;
        }

        m_constelBuffer = std::make_shared<ConstellationDataBuffer>(1024);
        m_waterBuffer = std::make_shared<WaterfallDataBuffer>();
        m_sharedFeat = std::make_shared<SharedFeatures>();

        // Recreate the dashboard plots to bind to new buffers
        if (m_ui.waterfallWidget) m_ui.waterfallWidget->deleteLater();
        if (m_ui.constellationWidget) m_ui.constellationWidget->deleteLater();
        
        m_ui.waterfallWidget = new WaterfallPlotWidget(m_waterBuffer, m_sharedFeat, m_dashboardScreen);
        m_ui.constellationWidget = new ConstellationPlotWidget(m_constelBuffer, m_sharedFeat, m_dashboardScreen);

        // Update layout with new widgets
        QLayoutItem* item;
        while ((item = m_waterfallLayout->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        while ((item = m_constelLayout->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }

        m_waterfallLayout->addWidget(m_ui.waterfallWidget, 1);
        m_constelLayout->addWidget(m_ui.constellationWidget, 1);

        // Connect slider signals for new waterfall widget
        disconnect(m_historySlider, nullptr, nullptr, nullptr);
        connect(m_historySlider, &QSlider::valueChanged, m_ui.waterfallWidget, &WaterfallPlotWidget::setScrollOffset);
        connect(m_ui.waterfallWidget, &WaterfallPlotWidget::scrollOffsetChanged, m_historySlider, &QSlider::setValue);
        connect(m_ui.waterfallWidget, &WaterfallPlotWidget::maxHistoryChanged, m_historySlider, &QSlider::setMaximum);
        
        connect(m_btnPlay, &QPushButton::clicked, m_ui.waterfallWidget, &WaterfallPlotWidget::play);
        connect(m_btnPause, &QPushButton::clicked, m_ui.waterfallWidget, &WaterfallPlotWidget::pause);
        connect(m_btnLive, &QPushButton::clicked, m_ui.waterfallWidget, &WaterfallPlotWidget::jumpToLive);
        connect(m_btnHistory, &QPushButton::clicked, m_ui.waterfallWidget, &WaterfallPlotWidget::jumpToHistory);

        m_historySlider->setValue(0);
        m_historySlider->setMaximum(0);

        m_isRunning = true;
        m_dspWorker = std::thread(dspPlaybackThread, data, m_constelBuffer, m_waterBuffer, m_sharedFeat, std::ref(m_isRunning));

        m_uiTimer->start(33);
        m_stackedWidget->setCurrentIndex(2);
    }

    void stopAnalysis() {
        m_isRunning = false;
        if (m_dspWorker.joinable()) {
            m_dspWorker.join();
        }
        m_uiTimer->stop();
    }

    QVBoxLayout* m_waterfallLayout = nullptr;
    QVBoxLayout* m_constelLayout = nullptr;
    QSlider* m_historySlider = nullptr;
    QPushButton* m_btnPlay = nullptr;
    QPushButton* m_btnPause = nullptr;
    QPushButton* m_btnLive = nullptr;
    QPushButton* m_btnHistory = nullptr;

    void initDashboardScreen() {
        m_dashboardScreen = new QWidget();
        QVBoxLayout* mainLayout = new QVBoxLayout(m_dashboardScreen);
        mainLayout->setContentsMargins(20, 10, 20, 10);
        mainLayout->setSpacing(10);

        // 1. TOP NAVIGATION
        QHBoxLayout* navLayout = new QHBoxLayout();
        QLabel* lblTitle = createLabel("NTRO Unified Stage 4 \xe2\x80\x93 Signal Analysis Dashboard", "#FFFFFF", true, 20);
        navLayout->addWidget(lblTitle);
        navLayout->addStretch(1);

        auto createStatus = [](const QString& name) {
            QLabel* l = createLabel(name + " \xe2\x97\x8f ACTIVE", "#888888", true, 13);
            return l;
        };
        m_ui.stData = createStatus("DATA INGESTION");
        m_ui.stData->setToolTip("File is being read from disk.");
        m_ui.stMmap = createStatus("mmap()");
        m_ui.stMmap->setToolTip("File loaded into memory without copying it.");
        m_ui.stBuffer = createStatus("BUFFER");
        m_ui.stBuffer->setToolTip("Data temporarily stored for processing.");
        m_ui.stDSP = createStatus("DSP");
        m_ui.stDSP->setToolTip("Digital Signal Processing applies math to interpret the signal.");
        m_ui.stWater = createStatus("WATERFALL");
        m_ui.stWater->setToolTip("Visualization of signal power over time and frequency.");
        m_ui.stIQ = createStatus("IQ ANALYSIS");
        m_ui.stIQ->setToolTip("Visualization of the raw signal components (In-Phase and Quadrature).");

        navLayout->addWidget(m_ui.stData); navLayout->addSpacing(20);
        navLayout->addWidget(m_ui.stMmap); navLayout->addSpacing(20);
        navLayout->addWidget(m_ui.stBuffer); navLayout->addSpacing(20);
        navLayout->addWidget(m_ui.stDSP); navLayout->addSpacing(20);
        navLayout->addWidget(m_ui.stWater); navLayout->addSpacing(20);
        navLayout->addWidget(m_ui.stIQ);
        
        QPushButton* btnHelp = new QPushButton("?");
        btnHelp->setFixedSize(24, 24);
        btnHelp->setStyleSheet("QPushButton { background-color: #333333; color: white; border-radius: 12px; font-weight: bold; } QPushButton:hover { background-color: #555555; }");
        navLayout->addSpacing(20);
        navLayout->addWidget(btnHelp);

        connect(btnHelp, &QPushButton::clicked, this, [this]() {
            QDialog helpDialog(this);
            helpDialog.setWindowTitle("Manual & Glossary");
            helpDialog.resize(500, 400);
            helpDialog.setStyleSheet("background-color: #111111; color: white;");
            QVBoxLayout* dl = new QVBoxLayout(&helpDialog);
            QTextBrowser* tb = new QTextBrowser();
            tb->setHtml(
                "<h3>Glossary & Manual</h3>"
                "<b>Waterfall</b>: Visualization of signal power over time and frequency. Bright areas mean strong signal energy.<br><br>"
                "<b>IQ Constellation</b>: Shows the raw In-Phase (I) and Quadrature (Q) components of the signal as a scatter plot. "
                "A tight ring indicates a clean constant-amplitude signal. Scattered dots indicate a noisy signal. Clusters indicate digital symbols.<br><br>"
                "<b>SNR (Signal-to-Noise Ratio)</b>: How much stronger the signal is compared to the background noise. Higher is better.<br><br>"
                "<b>Noise Floor</b>: The typical background energy level. Measured automatically from the data.<br><br>"
                "<b>FFTW</b>: used in waterfall.cpp for the waterfall FFT.<br><br>"
                "<b>Cyclostationary analysis</b>: not implemented in current code — planned Phase 2.<br>"
            );
            tb->setStyleSheet("font-size: 14px; border: none;");
            dl->addWidget(tb);
            helpDialog.exec();
        });

        QPushButton* btnLoadAnother = new QPushButton("Load Another File");
        btnLoadAnother->setStyleSheet("QPushButton { background-color: #444444; color: white; padding: 5px 10px; border-radius: 3px; font-weight: bold; } QPushButton:hover { background-color: #666666; }");
        navLayout->addSpacing(10);
        navLayout->addWidget(btnLoadAnother);
        connect(btnLoadAnother, &QPushButton::clicked, this, [this]() {
            stopAnalysis();
            m_stackedWidget->setCurrentIndex(1);
        });

        mainLayout->addLayout(navLayout);

        QFrame* line1 = new QFrame();
        line1->setFrameShape(QFrame::HLine);
        line1->setStyleSheet("background-color: #333333;");
        mainLayout->addWidget(line1);

        // 2. INPUT / FILE INFORMATION
        QHBoxLayout* infoLayout = new QHBoxLayout();
        m_ui.lblFile = createLabel("INPUT FILE: --", "#CCCCCC", false, 14);
        m_ui.lblFormat = createLabel("FORMAT: --", "#CCCCCC", false, 14);
        m_ui.lblSRate = createLabel("SAMPLE RATE: --", "#CCCCCC", false, 14);
        m_ui.lblCenter = createLabel("FREQUENCY: --", "#CCCCCC", false, 14);
        m_ui.lblBW = createLabel("BANDWIDTH: --", "#CCCCCC", false, 14);

        infoLayout->addWidget(m_ui.lblFile); infoLayout->addStretch(1);
        infoLayout->addWidget(m_ui.lblFormat); infoLayout->addStretch(1);
        infoLayout->addWidget(m_ui.lblSRate); infoLayout->addStretch(1);
        infoLayout->addWidget(m_ui.lblCenter); infoLayout->addStretch(1);
        infoLayout->addWidget(m_ui.lblBW);
        mainLayout->addLayout(infoLayout);

        // 3. SIGNAL HEALTH
        QHBoxLayout* healthLayout = new QHBoxLayout();
        m_ui.hlthPower = createLabel("SIGNAL POWER: --", "#CCCCCC", false, 14, "How strong the signal is.");
        m_ui.hlthNoise = createLabel("NOISE FLOOR: --", "#CCCCCC", false, 14, "Typical background signal level.");
        m_ui.hlthSNR = createLabel("SNR: --", "#CCCCCC", false, 14, "How much stronger the signal is than the noise.");
        m_ui.hlthSNRRatio = createLabel("SNR RATIO: --", "#CCCCCC", false, 14);
        m_ui.hlthQuality = createLabel("SIGNAL QUALITY: --", "#CCCCCC", false, 14);
        m_ui.hlthInterf = createLabel("INTERFERENCE: --", "#CCCCCC", false, 14);
        m_ui.hlthStab = createLabel("SIGNAL STABILITY: --", "#CCCCCC", false, 14);

        healthLayout->addWidget(m_ui.hlthPower); healthLayout->addStretch(1);
        healthLayout->addWidget(m_ui.hlthNoise); healthLayout->addStretch(1);
        healthLayout->addWidget(m_ui.hlthSNR); healthLayout->addStretch(1);
        healthLayout->addWidget(m_ui.hlthSNRRatio); healthLayout->addStretch(1);
        healthLayout->addWidget(m_ui.hlthQuality); healthLayout->addStretch(1);
        healthLayout->addWidget(m_ui.hlthInterf); healthLayout->addStretch(1);
        healthLayout->addWidget(m_ui.hlthStab);
        mainLayout->addLayout(healthLayout);

        QFrame* line2 = new QFrame();
        line2->setFrameShape(QFrame::HLine);
        line2->setStyleSheet("background-color: #333333;");
        mainLayout->addWidget(line2);

        // 4. MAIN VISUALIZATION AREA
        QHBoxLayout* vizLayout = new QHBoxLayout();
        
        // Left side: Waterfall + Controls
        QVBoxLayout* leftLayout = new QVBoxLayout();
        m_waterfallLayout = new QVBoxLayout();
        m_ui.waterfallWidget = nullptr; // initialized on play
        leftLayout->addLayout(m_waterfallLayout, 1);
        
        QHBoxLayout* waterControls = new QHBoxLayout();
        m_btnPlay = new QPushButton("\xe2\x96\xb6 PLAY");
        m_btnPause = new QPushButton("\xe2\x8f\xb8 PAUSE");
        m_btnLive = new QPushButton("LIVE");
        m_btnHistory = new QPushButton("HISTORY");
        QString btnStyle = "QPushButton { background-color: #222222; color: white; border: 1px solid #555555; padding: 5px 15px; font-weight: bold; } "
                           "QPushButton:hover { background-color: #444444; }";
        m_btnPlay->setStyleSheet(btnStyle);
        m_btnPause->setStyleSheet(btnStyle);
        m_btnLive->setStyleSheet(btnStyle);
        m_btnHistory->setStyleSheet(btnStyle);

        waterControls->addWidget(m_btnPlay);
        waterControls->addWidget(m_btnPause);
        waterControls->addSpacing(10);
        waterControls->addWidget(m_btnLive);
        waterControls->addWidget(m_btnHistory);
        
        m_historySlider = new QSlider(Qt::Horizontal);
        m_historySlider->setStyleSheet("QSlider::groove:horizontal { border: 1px solid #999999; height: 8px; background: #222222; margin: 2px 0; }"
                                     "QSlider::handle:horizontal { background: #555555; border: 1px solid #ffffff; width: 18px; margin: -2px 0; border-radius: 3px; }");
        m_historySlider->setMinimum(0);
        
        waterControls->addSpacing(20);
        waterControls->addWidget(m_historySlider, 1);
        
        leftLayout->addLayout(waterControls);
        vizLayout->addLayout(leftLayout, 1);

        // Right side: Constellation ONLY
        m_constelLayout = new QVBoxLayout();
        m_ui.constellationWidget = nullptr;
        
        vizLayout->addLayout(m_constelLayout, 1);
        mainLayout->addLayout(vizLayout, 1);

        QFrame* line3 = new QFrame();
        line3->setFrameShape(QFrame::HLine);
        line3->setStyleSheet("background-color: #333333;");
        mainLayout->addWidget(line3);

        // 5. mmap() VALIDATION & DATA FLOW
        QHBoxLayout* footerLayout = new QHBoxLayout();
        m_ui.lblPipeline = createLabel("\xe2\x9c\x93 INPUT \xe2\x86\x92 \xe2\x9c\x93 mmap() \xe2\x86\x92 \xe2\x9c\x93 BUFFER \xe2\x86\x92 \xe2\x9c\x93 DSP \xe2\x86\x92 \xe2\x9c\x93 VISUALIZATION", "#00FF00", true, 13);
        footerLayout->addWidget(m_ui.lblPipeline);
        footerLayout->addSpacing(40);
        
        QLabel* mmapValTitle = createLabel("DATA INGESTION / mmap()", "#CCCCCC", true, 13);
        m_ui.mmapVal1 = createLabel("MAPPED: --", "#CCCCCC", false, 13);
        m_ui.mmapVal2 = createLabel("SAMPLES RECEIVED: --", "#CCCCCC", false, 13);
        m_ui.mmapVal3 = createLabel("SAMPLES PROCESSED: --", "#CCCCCC", false, 13);
        m_ui.mmapVal5 = createLabel("DROPPED: 0", "#CCCCCC", false, 13);
        m_ui.mmapVal6 = createLabel("ERRORS: 0", "#00FF00", false, 13);

        footerLayout->addWidget(mmapValTitle); footerLayout->addSpacing(20);
        footerLayout->addWidget(m_ui.mmapVal1); footerLayout->addSpacing(15);
        footerLayout->addWidget(m_ui.mmapVal2); footerLayout->addSpacing(15);
        footerLayout->addWidget(m_ui.mmapVal3); footerLayout->addSpacing(15);
        footerLayout->addWidget(m_ui.mmapVal5); footerLayout->addSpacing(15);
        footerLayout->addWidget(m_ui.mmapVal6); footerLayout->addStretch(1);

        mainLayout->addLayout(footerLayout);
        m_stackedWidget->addWidget(m_dashboardScreen);
    }

    void updateDashboard() {
        if (!m_isRunning || !m_sharedFeat) return;

        std::lock_guard<std::mutex> lock(m_sharedFeat->mtx);
        const auto& f = m_sharedFeat->data;
        const auto& m = m_sharedFeat->metrics;

        bool isWorking = (m.samples_processed > 0);
        auto setStatus = [](QLabel* l, bool active, const QString& name) {
            if (active) {
                l->setText(name + " \xe2\x97\x8f ACTIVE");
                l->setStyleSheet("color: #00FF00; font-size: 13px; font-weight: bold;");
            } else {
                l->setText(name + " \xe2\x97\x8f INACTIVE");
                l->setStyleSheet("color: #888888; font-size: 13px; font-weight: bold;");
            }
        };
        setStatus(m_ui.stData, isWorking, "DATA INGESTION");
        setStatus(m_ui.stMmap, m.mmap_active && isWorking, "mmap()");
        setStatus(m_ui.stBuffer, isWorking, "BUFFER");
        setStatus(m_ui.stDSP, isWorking, "DSP");
        setStatus(m_ui.stWater, isWorking, "WATERFALL");
        setStatus(m_ui.stIQ, isWorking, "IQ ANALYSIS");

        QString sizeStr = formatBytes(m.mapped_size);
        m_ui.lblFile->setText(QString("INPUT FILE: %1 (%2)").arg(QString::fromStdString(m.file_name)).arg(sizeStr));
        
        QString formatStr;
        if (m.source_format == "WAV") formatStr = "WAV (Audio/sample signal)";
        else if (m.source_format == "IQ") formatStr = "IQ (In-phase + Quadrature signal)";
        else formatStr = QString::fromStdString(m.source_format);
        
        m_ui.lblFormat->setText(QString("FORMAT: %1  |  SAMPLES: %2 / %3")
                           .arg(formatStr)
                           .arg(m.samples_processed)
                           .arg(m.samples_received));

        m_ui.lblSRate->setText(QString("SAMPLE RATE: %1 MS/s").arg(f.sample_rate / 1e6, 0, 'f', 2));
        m_ui.lblCenter->setText(QString("FREQUENCY: %1 MHz").arg(f.center_freq / 1e6, 0, 'f', 1));
        m_ui.lblBW->setText(QString("BANDWIDTH: 2.048 MHz"));

        m_ui.hlthPower->setText(QString("SIGNAL POWER: %1 dB").arg(f.peak_power_db, 0, 'f', 1));
        m_ui.hlthNoise->setText(QString("NOISE FLOOR: %1 dB").arg(f.noise_floor_db, 0, 'f', 1));
        m_ui.hlthSNR->setText(QString("SNR: %1 dB").arg(f.snr_db, 0, 'f', 1));

        float ratio = std::pow(10.0f, f.snr_db / 10.0f);
        m_ui.hlthSNRRatio->setText(QString("SNR RATIO: %1:1").arg(static_cast<int>(ratio)));

        QString quality = "POOR";
        QString qColor = "#FF0000";
        if (f.snr_db > 20.0f) {
            quality = "GOOD"; qColor = "#00FF00";
        } else if (f.snr_db >= 10.0f) {
            quality = "FAIR"; qColor = "#FFFF00";
        }
        m_ui.hlthQuality->setText("SIGNAL QUALITY: " + quality);
        m_ui.hlthQuality->setStyleSheet("color: " + qColor + "; font-size: 14px; font-weight: bold;");

        if (f.interference_detected) {
            m_ui.hlthInterf->setText("INTERFERENCE: DETECTED");
            m_ui.hlthInterf->setStyleSheet("color: #FF0000; font-size: 14px; font-weight: bold;");
        } else {
            m_ui.hlthInterf->setText("INTERFERENCE: NONE");
            m_ui.hlthInterf->setStyleSheet("color: #00FF00; font-size: 14px; font-weight: bold;");
        }

        if (f.is_stable) {
            m_ui.hlthStab->setText("SIGNAL STABILITY: STABLE");
            m_ui.hlthStab->setStyleSheet("color: #00FF00; font-size: 14px; font-weight: bold;");
        } else {
            m_ui.hlthStab->setText("SIGNAL STABILITY: UNSTABLE");
            m_ui.hlthStab->setStyleSheet("color: #FFFF00; font-size: 14px; font-weight: bold;");
        }

        bool mmapVerified = m.mmap_active && m.samples_processed > 0 && m.mapped_address != 0;
        if (mmapVerified) {
            m_ui.lblPipeline->setText("\xe2\x9c\x93 INPUT \xe2\x86\x92 \xe2\x9c\x93 mmap() \xe2\x86\x92 \xe2\x9c\x93 BUFFER \xe2\x86\x92 \xe2\x9c\x93 DSP \xe2\x86\x92 \xe2\x9c\x93 VISUALIZATION");
            m_ui.lblPipeline->setStyleSheet("color: #00FF00; font-size: 13px; font-weight: bold;");
        } else if (m.mmap_active && m.samples_processed == 0) {
            m_ui.lblPipeline->setText("\xe2\x9c\x93 INPUT \xe2\x86\x92 \xe2\x9a\xa0 mmap() \xe2\x86\x92 \xe2\x9c\x95 BUFFER \xe2\x86\x92 \xe2\x9c\x95 DSP \xe2\x86\x92 \xe2\x9c\x95 VISUALIZATION");
            m_ui.lblPipeline->setStyleSheet("color: #FFFF00; font-size: 13px; font-weight: bold;");
        } else {
            m_ui.lblPipeline->setText("\xe2\x9c\x95 INPUT \xe2\x86\x92 \xe2\x9c\x95 mmap() \xe2\x86\x92 \xe2\x9c\x95 BUFFER \xe2\x86\x92 \xe2\x9c\x95 DSP \xe2\x86\x92 \xe2\x9c\x95 VISUALIZATION");
            m_ui.lblPipeline->setStyleSheet("color: #FF0000; font-size: 13px; font-weight: bold;");
        }
        
        m_ui.mmapVal1->setText(QString("MAPPED: 0x%1").arg(m.mapped_address, 0, 16));
        m_ui.mmapVal2->setText(QString("SAMPLES RECEIVED: %1").arg(m.samples_received));
        m_ui.mmapVal3->setText(QString("SAMPLES PROCESSED: %1").arg(m.samples_processed));

        static bool reported = false;
        if (!reported && m.samples_processed > 1000) {
            std::cout << "\\n=== REPORT FOR " << m.file_name << " ===" << std::endl;
            std::cout << "FILE TYPE: " << formatStr.toStdString() << std::endl;
            std::cout << "FILE SIZE: " << sizeStr.toStdString() << std::endl;
            std::cout << "TOTAL SAMPLES: " << m.samples_received << std::endl;
            std::cout << "SNR: " << f.snr_db << " dB" << std::endl;
            std::cout << "=======================================\\n" << std::endl;
            reported = true;
        }
    }
};

#include "main_qt.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyleSheet("QToolTip { color: #ffffff; background-color: #333333; border: 1px solid white; font-size: 12px; padding: 4px; }");

    DashboardApp dashboard;
    dashboard.show();

    return app.exec();
}
"""

with open('d:/SIH 2026/visualization/src/main_qt.cpp', 'w', encoding='utf-8') as f:
    f.write(cpp_code)
