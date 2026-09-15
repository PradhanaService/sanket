#include "ConstellationPlotWidget.hpp"
#include <QPainter>
#include <QColor>
#include <QPen>
#include <QPalette>
#include <QMouseEvent>
#include <cmath>

ConstellationPlotWidget::ConstellationPlotWidget(std::shared_ptr<ConstellationDataBuffer> dataBuffer,
                                                 std::shared_ptr<SharedFeatures> sharedFeat,
                                                 QWidget* parent)
    : QWidget(parent), m_buffer(std::move(dataBuffer)), m_sharedFeat(std::move(sharedFeat)) {
    
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setAutoFillBackground(true);
    setPalette(pal);

    m_localFrame.reserve(8192);

    connect(&m_renderTimer, &QTimer::timeout, this, &ConstellationPlotWidget::updateData);
    m_renderTimer.start(33); // 30 FPS
}

void ConstellationPlotWidget::mouseMoveEvent(QMouseEvent* /*event*/) {
    // Tooltips removed for clean UI
}

void ConstellationPlotWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_decayBuffer.size() != size()) {
        m_decayBuffer = QImage(size(), QImage::Format_ARGB32_Premultiplied);
        m_decayBuffer.fill(Qt::transparent);
    }
}

void ConstellationPlotWidget::updateData() {
    if (m_buffer) {
        m_buffer->pull(m_localFrame);
        update();
    }
}

void ConstellationPlotWidget::play() {
    if (!m_renderTimer.isActive()) {
        m_renderTimer.start(33);
    }
}

void ConstellationPlotWidget::pause() {
    m_renderTimer.stop();
}

void ConstellationPlotWidget::paintEvent(QPaintEvent* /*event*/) {
    int w = width();
    int h = height();

    QPainter decayPainter(&m_decayBuffer);
    decayPainter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    decayPainter.fillRect(m_decayBuffer.rect(), QColor(0, 0, 0, 30));
    decayPainter.end();

    QPainter pointPainter(&m_decayBuffer);
    pointPainter.setRenderHint(QPainter::Antialiasing, true);
    pointPainter.setPen(Qt::NoPen);
    pointPainter.setBrush(QColor(0, 255, 120, 240)); // BRIGHTER GREEN DOTS

    int center_x = w / 2;
    int center_y = h / 2;
    
    if (!m_localFrame.empty()) {
        float max_abs = 0.0f;
        for (const auto& s : m_localFrame) {
            float abs_i = std::abs(s.real());
            float abs_q = std::abs(s.imag());
            if (abs_i > max_abs) max_abs = abs_i;
            if (abs_q > max_abs) max_abs = abs_q;
        }
        if (max_abs < 1e-6f) max_abs = 1e-6f;
        
        float scale = (std::min(w, h) * 0.45f) / (max_abs * 1.2f); // Auto-scale with 20% margin

        for (const auto& sample : m_localFrame) {
            float x = center_x + sample.real() * scale;
            float y = center_y - sample.imag() * scale;
            pointPainter.drawEllipse(QPointF(x, y), 3.0, 3.0); // SLIGHTLY LARGER DOTS
        }
    }
    pointPainter.end();

    QPainter screenPainter(this);
    screenPainter.fillRect(rect(), Qt::black);
    screenPainter.drawImage(0, 0, m_decayBuffer);

    SignalFeatures feat;
    if (m_sharedFeat) {
        std::lock_guard<std::mutex> lock(m_sharedFeat->mtx);
        feat = m_sharedFeat->data;
    }

    // Polar Grid
    screenPainter.setPen(QPen(QColor(40, 40, 40), 1, Qt::DotLine));
    float max_radius = std::min(w, h) * 0.45f;
    for (int i = 1; i <= 4; ++i) {
        float r = max_radius * (i / 4.0f);
        screenPainter.drawEllipse(QPointF(center_x, center_y), r, r);
    }
    
    // Diagonal lines for the grid
    screenPainter.drawLine(center_x - max_radius*0.707, center_y - max_radius*0.707, center_x + max_radius*0.707, center_y + max_radius*0.707);
    screenPainter.drawLine(center_x - max_radius*0.707, center_y + max_radius*0.707, center_x + max_radius*0.707, center_y - max_radius*0.707);

    // Crosshairs
    screenPainter.setPen(QPen(QColor(80, 80, 80), 1, Qt::DashLine));
    screenPainter.drawLine(center_x, 0, center_x, h);
    screenPainter.drawLine(0, center_y, w, center_y);
    
    screenPainter.setPen(QColor(150, 150, 150));
    screenPainter.drawText(center_x + 10, 20, QString::fromUtf8("Q (Quadrature)"));
    screenPainter.drawText(center_x + 10, 35, QString::fromUtf8("\xe2\x86\x91"));
    screenPainter.drawText(w - 120, center_y - 10, QString::fromUtf8("I (In-Phase) \xe2\x86\x92"));
    
    screenPainter.setPen(Qt::yellow);
    screenPainter.drawText(center_x + 5, center_y + 15, QString::fromUtf8("I=0, Q=0 (Center Reference)"));

    // 1. Titles
    screenPainter.setPen(Qt::white);
    screenPainter.drawText(10, 20, "IQ CONSTELLATION");

    if (m_localFrame.empty()) {
        screenPainter.setPen(Qt::yellow);
        screenPainter.drawText(center_x - 40, center_y - 20, "NO IQ DATA");
        screenPainter.setPen(QColor(150, 150, 150));
        screenPainter.drawText(center_x - 60, center_y + 10, "Waiting for IQ samples.");
    }

    // 2. Metrics (Top Right)
    screenPainter.fillRect(w - 230, 10, 220, 110, QColor(0, 0, 0, 180));
    screenPainter.setPen(Qt::white);
    QString modStr = feat.modulation_type.empty() ? "UNKNOWN" : QString::fromStdString(feat.modulation_type);
    screenPainter.drawText(w - 220, 25, QString("MODULATION: %1").arg(modStr));
    screenPainter.drawText(w - 220, 45, QString("SAMPLE COUNT: %1").arg(m_localFrame.size()));
    screenPainter.drawText(w - 220, 65, QString("EVM: %1 %").arg(feat.estimated_evm, 0, 'f', 1));
    screenPainter.drawText(w - 220, 85, QString("PHASE STABILITY: %1 %").arg(feat.phase_stability, 0, 'f', 1));
    screenPainter.drawText(w - 220, 105, QString("AMPLITUDE STABILITY: %1 %").arg(feat.amplitude_stability, 0, 'f', 1));

    // 3. Explanation (Bottom)
    int lh = 135;
    int lw = 380;
    screenPainter.fillRect(10, h - lh - 10, lw, lh, QColor(0, 0, 0, 180));
    screenPainter.setPen(Qt::white);
    screenPainter.drawText(20, h - lh + 10, "WHAT YOU ARE SEEING");
    screenPainter.setPen(QColor(150, 150, 150));
    screenPainter.drawText(20, h - lh + 30, "Each dot = one measured IQ sample.");
    screenPainter.drawText(20, h - lh + 45, "I = In-phase component");
    screenPainter.drawText(20, h - lh + 60, "Q = Quadrature component");
    screenPainter.drawText(20, h - lh + 80, "Tight points = more organized signal");
    screenPainter.drawText(20, h - lh + 95, "Scattered points = more noise/distortion");
    screenPainter.drawText(20, h - lh + 110, "Multiple clusters = possible digital symbols");
    screenPainter.drawText(20, h - lh + 125, "Ring = approximately constant amplitude with changing phase");
}
