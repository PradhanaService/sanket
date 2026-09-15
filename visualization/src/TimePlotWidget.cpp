#include "TimePlotWidget.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QPen>
#include <cmath>

TimePlotWidget::TimePlotWidget(std::shared_ptr<ConstellationDataBuffer> dataBuffer,
                               std::shared_ptr<SharedFeatures> sharedFeat,
                               QWidget* parent)
    : QWidget(parent), m_buffer(std::move(dataBuffer)), m_sharedFeat(std::move(sharedFeat)) {
    
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setAutoFillBackground(true);
    setPalette(pal);

    connect(&m_renderTimer, &QTimer::timeout, this, &TimePlotWidget::updateData);
    m_renderTimer.start(33); // 30 FPS
}

void TimePlotWidget::mouseMoveEvent(QMouseEvent* /*event*/) {
    // Interactive tooltips removed for clean layout
}

void TimePlotWidget::updateData() {
    if (m_buffer) {
        m_buffer->pull(m_localFrame);
        update();
    }
}

void TimePlotWidget::paintEvent(QPaintEvent* /*event*/) {
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

    if (!m_localFrame.empty()) {
        QPainterPath pathI, pathQ;
        size_t display_count = std::min<size_t>(m_localFrame.size(), 1024);
        float x_step = static_cast<float>(w) / display_count;
        
        for (size_t i = 0; i < display_count; ++i) {
            float x = i * x_step;
            float i_val = m_localFrame[i].real();
            float q_val = m_localFrame[i].imag();
            
            float y_i = h / 2 - (i_val * h / 4.0f);
            float y_q = h / 2 - (q_val * h / 4.0f);

            if (i == 0) {
                pathI.moveTo(x, y_i);
                pathQ.moveTo(x, y_q);
            } else {
                pathI.lineTo(x, y_i);
                pathQ.lineTo(x, y_q);
            }
        }
        painter.setPen(QPen(QColor(0, 255, 255, 200), 1.5)); // Cyan for I
        painter.drawPath(pathI);
        painter.setPen(QPen(QColor(0, 255, 0, 150), 1.5)); // Green for Q
        painter.drawPath(pathQ);
    }

    // 1. Titles
    painter.setPen(Qt::white);
    painter.drawText(10, 20, "3. TIME-DOMAIN I/Q");
    painter.setPen(QColor(150, 150, 150));
    painter.drawText(10, 35, "Signal amplitude over time");

    // 2. Axes
    painter.fillRect(10, h - 75, 220, 25, QColor(0, 0, 0, 150));
    painter.setPen(Qt::white);
    painter.drawText(15, h - 58, "X: Time (ms) | Y: Amplitude");

    // 3. Legend (Top Right - clean block)
    painter.fillRect(w - 150, 10, 140, 50, QColor(0, 0, 0, 150));
    painter.setPen(QColor(0, 255, 255));
    painter.drawText(w - 140, 25, "\xe2\x96\xac CYAN: I");
    painter.setPen(QColor(0, 255, 0));
    painter.drawText(w - 140, 45, "\xe2\x96\xac GREEN: Q");
    
    painter.setPen(Qt::white);
    painter.drawText(w - 70, 25, "\xe2\x80\x94 In-phase");
    painter.drawText(w - 70, 45, "\xe2\x80\x94 Quadrature");

    // 4. Metrics & Explanations (Bottom)
    painter.fillRect(10, h - 45, 300, 35, QColor(0, 0, 0, 150));
    painter.setPen(QColor(150, 150, 150));
    painter.drawText(15, h - 22, "I and Q are two components of the received RF signal.");

    // Dynamic metrics on the right side of the bottom
    painter.fillRect(w - 400, h - 35, 390, 25, QColor(0, 0, 0, 150));
    painter.setPen(Qt::white);
    QString metrics = QString("Frequency: %1 MHz | Amplitude: %2 dB | Stability: %3")
                      .arg(feat.center_freq / 1e6, 0, 'f', 2)
                      .arg(feat.peak_power_db, 0, 'f', 1)
                      .arg(feat.is_stable ? "STABLE" : "UNSTABLE");
    painter.drawText(w - 390, h - 18, metrics);
}
