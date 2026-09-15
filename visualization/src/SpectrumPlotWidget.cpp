#include "SpectrumPlotWidget.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QPen>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

SpectrumPlotWidget::SpectrumPlotWidget(std::shared_ptr<WaterfallDataBuffer> waterBuffer,
                                       std::shared_ptr<ConstellationDataBuffer> constelBuffer,
                                       std::shared_ptr<SharedFeatures> sharedFeat,
                                       QWidget* parent)
    : QWidget(parent), 
      m_waterBuffer(std::move(waterBuffer)), 
      m_constelBuffer(std::move(constelBuffer)),
      m_sharedFeat(std::move(sharedFeat)) {
    
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setAutoFillBackground(true);
    setPalette(pal);

    connect(&m_renderTimer, &QTimer::timeout, this, &SpectrumPlotWidget::updateData);
    m_renderTimer.start(33); // 30 FPS
}

void SpectrumPlotWidget::mouseMoveEvent(QMouseEvent* /*event*/) {
    // Interactive tooltips removed for clean layout
}

void SpectrumPlotWidget::updateData() {
    bool dirty = false;
    if (m_waterBuffer && m_waterBuffer->get_latest(m_latestSpectrumRow)) dirty = true;
    if (m_constelBuffer) {
        m_constelBuffer->pull(m_latestTimeRow);
        dirty = true;
    }
    if (dirty) update();
}

void SpectrumPlotWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), Qt::black); 

    int w = width();
    int h = height();

    SignalFeatures feat;
    if (m_sharedFeat) {
        std::lock_guard<std::mutex> lock(m_sharedFeat->mtx);
        feat = m_sharedFeat->data;
    }

    // Grid (subtle technical look)
    painter.setPen(QPen(QColor(40, 40, 40), 1, Qt::DotLine));
    for (int i = 1; i < 10; ++i) {
        int x = w * i / 10;
        painter.drawLine(x, 0, x, h);
        int y = h * i / 10;
        painter.drawLine(0, y, w, y);
    }

    float agc_min = feat.noise_floor_db - 10.0f; 
    float agc_max = feat.peak_power_db + 10.0f;
    float range = (agc_max - agc_min) > 1.0f ? (agc_max - agc_min) : 1.0f;

    float peak_x = -100.0f;
    float noise_y = -100.0f;

    // 1. Draw FFT Spectrum (Clean Green Line)
    if (!m_latestSpectrumRow.empty()) {
        std::vector<float> row(m_latestSpectrumRow.size());
        size_t half = m_latestSpectrumRow.size() / 2;
        for(size_t k = 0; k < half; ++k) {
            row[k] = m_latestSpectrumRow[half + k];
            row[half + k] = m_latestSpectrumRow[k];
        }

        QPainterPath path;
        for (size_t i = 0; i < row.size(); ++i) {
            float x = static_cast<float>(i) * w / row.size();
            float norm = (row[i] - agc_min) / range;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            
            float y = h - (norm * h);
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        painter.setPen(QPen(QColor(0, 255, 0), 1.0));
        painter.drawPath(path);

        if (feat.snr_db > 10.0f) {
            int peak_idx = feat.peak_freq_bin;
            if (peak_idx < half) peak_idx += half;
            else peak_idx -= half;
            peak_x = static_cast<float>(peak_idx) * w / row.size();
        }
    }

    // 2. Draw NOISE FLOOR marker
    float noise_norm = (feat.noise_floor_db - agc_min) / range;
    if (noise_norm >= 0.0f && noise_norm <= 1.0f) {
        noise_y = h - (noise_norm * h);
        
        // Subtle shaded region below noise floor
        painter.fillRect(0, noise_y, w, h - noise_y, QColor(0, 50, 0, 30));

        painter.setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
        painter.drawLine(0, noise_y, w, noise_y);
        
        // Label firmly on the left edge, slightly above the dashed line
        painter.setPen(Qt::white);
        painter.drawText(5, noise_y - 20, "NOISE FLOOR");
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(5, noise_y - 5, QString("%1 dB").arg(feat.noise_floor_db, 0, 'f', 1));
    }

    // 3. Draw PRIMARY SIGNAL marker
    if (peak_x > 0.0f) {
        painter.setPen(QPen(QColor(255, 0, 0), 1, Qt::DashLine));
        painter.drawLine(peak_x, 0, peak_x, h);

        // Marker label in the top-right corner to NEVER overlap the peak waveform
        painter.setPen(Qt::white);
        painter.drawText(w - 150, 20, "PRIMARY SIGNAL");
        painter.setPen(QColor(0, 255, 0));
        painter.drawText(w - 150, 35, QString("%1 MHz").arg(feat.center_freq / 1e6, 0, 'f', 2));
        painter.drawText(w - 150, 50, QString("%1 dB").arg(feat.peak_power_db, 0, 'f', 1));
    }

    if (feat.interference_detected && peak_x > 0.0f) {
        // Offset horizontally from the primary peak to avoid overlap
        float int_x = (peak_x < w / 2) ? peak_x + 100 : peak_x - 150;
        painter.setPen(Qt::red);
        painter.drawText(int_x, h / 2, "INTERFERENCE");
        painter.drawText(int_x, h / 2 + 15, "Unwanted Energy");
    }

    // 4. Titles
    painter.setPen(Qt::white);
    painter.drawText(10, 20, "1. FREQUENCY SPECTRUM");
    painter.setPen(QColor(150, 150, 150));
    painter.drawText(10, 35, "Signal strength across frequency");
    
    // Bottom Axes Labels
    painter.fillRect(10, h - 35, 230, 25, QColor(0, 0, 0, 150));
    painter.setPen(Qt::white);
    painter.drawText(15, h - 18, "X: Frequency (MHz) | Y: Power (dB)");

    // Bottom Annotations
    painter.fillRect(10, h - 70, 220, 30, QColor(0, 0, 0, 150));
    painter.setPen(Qt::white);
    painter.drawText(15, h - 55, "NOISE FLOOR");
    painter.setPen(QColor(150, 150, 150));
    painter.drawText(15, h - 42, "Typical background RF energy");

    painter.fillRect(w - 230, h - 70, 220, 30, QColor(0, 0, 0, 150));
    painter.setPen(Qt::white);
    painter.drawText(w - 225, h - 55, "PRIMARY SIGNAL");
    painter.setPen(QColor(150, 150, 150));
    painter.drawText(w - 225, h - 42, "Strongest detected RF energy");
}
