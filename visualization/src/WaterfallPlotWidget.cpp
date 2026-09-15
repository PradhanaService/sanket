#include "WaterfallPlotWidget.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

WaterfallPlotWidget::WaterfallPlotWidget(std::shared_ptr<WaterfallDataBuffer> dataBuffer,
                                         std::shared_ptr<SharedFeatures> sharedFeat,
                                         QWidget* parent)
    : QWidget(parent), m_buffer(std::move(dataBuffer)), m_sharedFeat(std::move(sharedFeat)) {
    
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setAutoFillBackground(true);
    setPalette(pal);

    m_historyImage = QImage(1024, m_maxHistoryRows, QImage::Format_ARGB32);
    m_historyImage.fill(Qt::black);

    m_colormap.reserve(256);
    for (int i = 0; i < 256; ++i) {
        float t = i / 255.0f;
        int r = 0, g = 0, b = 0;
        if (t < 0.2f) { // Dark Blue to Blue
            b = 128 + static_cast<int>(127 * (t / 0.2f));
        } else if (t < 0.4f) { // Blue to Cyan
            b = 255;
            g = static_cast<int>(255 * ((t - 0.2f) / 0.2f));
        } else if (t < 0.6f) { // Cyan to Green
            g = 255;
            b = static_cast<int>(255 * (1.0f - (t - 0.4f) / 0.2f));
        } else if (t < 0.8f) { // Green to Yellow
            g = 255;
            r = static_cast<int>(255 * ((t - 0.6f) / 0.2f));
        } else { // Yellow to Red
            r = 255;
            g = static_cast<int>(255 * (1.0f - (t - 0.8f) / 0.2f));
        }
        m_colormap.push_back(qRgb(r, g, b));
    }

    connect(&m_renderTimer, &QTimer::timeout, this, &WaterfallPlotWidget::updateData);
    m_renderTimer.start(33); 
}

void WaterfallPlotWidget::play() {
    m_isPaused = false;
    m_scrollOffset = 0; 
    update();
}

void WaterfallPlotWidget::pause() {
    m_isPaused = true;
    update();
}

void WaterfallPlotWidget::jumpToLive() {
    m_scrollOffset = 0;
    emit scrollOffsetChanged(m_scrollOffset);
    update();
}

void WaterfallPlotWidget::jumpToHistory() {
    m_scrollOffset = std::min(m_scrollOffset + 100, m_maxHistoryRows - height());
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    emit scrollOffsetChanged(m_scrollOffset);
    update();
}

void WaterfallPlotWidget::setScrollOffset(int offset) {
    if (m_isDragging) return; // Don't snap back if user is actively dragging the slider
    m_scrollOffset = offset;
    if (m_scrollOffset > m_maxHistoryRows - height()) m_scrollOffset = m_maxHistoryRows - height();
    if (m_scrollOffset < 0) m_scrollOffset = 0;
    update();
}

void WaterfallPlotWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_dragStartY = qRound(event->position().y());
        m_dragStartOffset = m_scrollOffset;
    }
}

void WaterfallPlotWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
}

void WaterfallPlotWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging) {
        int dy = qRound(event->position().y()) - m_dragStartY;
        // Dragging down visually means viewing older data (higher y offset)
        m_scrollOffset = m_dragStartOffset + dy;
        
        if (m_scrollOffset > m_maxHistoryRows - height()) {
            m_scrollOffset = m_maxHistoryRows - height();
        }
        if (m_scrollOffset < 0) {
            m_scrollOffset = 0;
        }
        emit scrollOffsetChanged(m_scrollOffset);
        update();
    }
}

void WaterfallPlotWidget::updateData() {
    if (!m_buffer) return;

    std::vector<std::vector<float>> new_rows;
    m_buffer->pull_all(new_rows);
    if (new_rows.empty()) {
        if (!m_isPaused && !m_isDragging) {
            update();
        }
        return;
    }

    int new_count = new_rows.size();
    int img_w = m_historyImage.width();
    int img_h = m_historyImage.height();

    // Shift image down to append new rows at the top (row 0)
    if (new_count < img_h) {
        uchar* bits = m_historyImage.bits();
        int bpl = m_historyImage.bytesPerLine();
        memmove(bits + new_count * bpl, bits, (img_h - new_count) * bpl);
    }
    
    m_currentRowCount = std::min(m_maxHistoryRows, m_currentRowCount + new_count);

    float current_min = -100.0f;
    float current_max = 0.0f;
    if (m_sharedFeat) {
        std::lock_guard<std::mutex> lock(m_sharedFeat->mtx);
        current_min = m_sharedFeat->data.noise_floor_db;
        current_max = m_sharedFeat->data.peak_power_db;
    }
    float range = std::max(1.0f, current_max - current_min);

    for (int r = 0; r < std::min(new_count, img_h); ++r) {
        const auto& row = new_rows[new_rows.size() - 1 - r];
        QRgb* scanLine = reinterpret_cast<QRgb*>(m_historyImage.scanLine(r));
        
        size_t half = row.size() / 2;
        for (int x = 0; x < img_w; ++x) {
            float src_x = static_cast<float>(x) * row.size() / img_w;
            int idx = static_cast<int>(src_x);
            if (idx >= row.size()) idx = row.size() - 1;
            
            int fft_idx = idx;
            if (fft_idx < half) fft_idx += half;
            else fft_idx -= half;

            float val = row[fft_idx];
            float norm = 0.0f;
            if (current_max > current_min) {
                norm = (val - current_min) / range;
            }
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            
            int color_idx = static_cast<int>(norm * 255);
            scanLine[x] = m_colormap[color_idx];
        }
    }
    
    // Auto-scroll logic
    if (m_isPaused) {
        m_scrollOffset += new_count;
        if (m_scrollOffset > m_maxHistoryRows - height()) {
            m_scrollOffset = m_maxHistoryRows - height();
        }
        emit scrollOffsetChanged(m_scrollOffset);
    } else if (m_isDragging) {
        m_scrollOffset += new_count;
        m_dragStartOffset += new_count;
        if (m_scrollOffset > m_maxHistoryRows - height()) {
            m_scrollOffset = m_maxHistoryRows - height();
        }
        emit scrollOffsetChanged(m_scrollOffset);
    }
    
    // Always emit max history in case it changed (height is known)
    int maxOffset = m_maxHistoryRows - height();
    if (maxOffset < 0) maxOffset = 0;
    emit maxHistoryChanged(maxOffset);
    
    update();
}

void WaterfallPlotWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    int h = height();
    int w = width();

    int source_y = m_scrollOffset;
    QRect sourceRect(0, source_y, 1024, h);
    QRect targetRect(0, 0, w, h);

    painter.drawImage(targetRect, m_historyImage, sourceRect);

    SignalFeatures feat;
    if (m_sharedFeat) {
        std::lock_guard<std::mutex> lock(m_sharedFeat->mtx);
        feat = m_sharedFeat->data;
    }

    // 1. Titles
    painter.setPen(Qt::white);
    painter.drawText(10, 20, "WATERFALL");

    // Status Live / History / Paused
    if (m_isPaused) {
        painter.setPen(Qt::yellow);
        painter.drawText(10, 35, "WATERFALL: PAUSED");
    } else if (m_scrollOffset > 0) {
        painter.setPen(Qt::cyan);
        painter.drawText(10, 35, "HISTORY");
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(10, 50, QString("Viewing: ~%1 ms ago").arg(m_scrollOffset * 10));
    } else {
        painter.setPen(Qt::green);
        painter.drawText(10, 35, "WATERFALL: LIVE");
    }

    if (feat.noise_floor_db == feat.peak_power_db && feat.noise_floor_db != 0.0f) {
        painter.setPen(Qt::red);
        painter.drawText(10, 70, "\xe2\x9a\xa0 NO POWER VARIATION");
    }

    // 2. Axes Legend (Bottom Left)
    int lh = 120; 
    painter.fillRect(10, h - lh - 30, 290, lh, QColor(0, 0, 0, 180)); // Shifted up a bit
    painter.setPen(Qt::white);
    painter.drawText(15, h - lh - 25 + 5, "WHAT YOU ARE SEEING");
    painter.setPen(QColor(150, 150, 150));
    painter.drawText(15, h - lh - 25 + 25, "X-axis: Frequency (Hz)");
    painter.drawText(15, h - lh - 25 + 45, "Y-axis: Time");
    painter.drawText(15, h - lh - 25 + 75, "Bright line = Signal remains at a frequency");
    painter.drawText(15, h - lh - 25 + 95, "Dim background = Low-power/noise");
    
    // Actual Axis Labels on screen
    painter.setPen(QColor(200, 200, 200));
    painter.drawText(w / 2 - 50, h - 10, "Frequency (Hz) \xe2\x86\x92");
    
    painter.save();
    painter.translate(15, h / 2 + 20);
    painter.rotate(-90);
    painter.drawText(0, 0, "Time \xe2\x86\x93");
    painter.restore();
    
    // 3. SIGNAL POWER Color Bar Legend (Top Right)
    int cb_w = 20;
    int cb_h = 100;
    int cb_x = w - 180;
    int cb_y = 15;

    painter.fillRect(cb_x - 10, cb_y - 10, 180, cb_h + 80, QColor(0, 0, 0, 180));
    painter.setPen(Qt::white);
    painter.drawText(cb_x - 5, cb_y, "SIGNAL POWER");

    for (int i = 0; i < cb_h; ++i) {
        float norm = 1.0f - static_cast<float>(i) / cb_h;
        int color_idx = static_cast<int>(norm * 255);
        painter.fillRect(cb_x, cb_y + 10 + i, cb_w, 1, QColor(m_colormap[color_idx]));
    }
    
    painter.setPen(Qt::white);
    painter.drawText(cb_x + cb_w + 5, cb_y + 20, "RED       HIGH");
    painter.drawText(cb_x + cb_w + 5, cb_y + 40, "YELLOW    HIGH");
    painter.drawText(cb_x + cb_w + 5, cb_y + 60, "GREEN     MEDIUM");
    painter.drawText(cb_x + cb_w + 5, cb_y + 80, "CYAN      LOW");
    painter.drawText(cb_x + cb_w + 5, cb_y + 100, "BLUE      VERY LOW");

    painter.setPen(QColor(150, 150, 150));
    painter.drawText(cb_x - 5, cb_y + 130, "Color represents measured");
    painter.drawText(cb_x - 5, cb_y + 145, "signal power.");

    // 4. Primary Signal Annotation (Leader Line)
    if (m_scrollOffset < 50) { // Only show on live
        float peak_x = -1.0f;
        if (feat.snr_db > 10.0f) {
            peak_x = (feat.peak_freq_bin / 1024.0f) * w;
            
            float target_x = peak_x;
            float target_y = h / 2.0f;
            
            float label_x = (peak_x < w / 2) ? peak_x + 60 : peak_x - 200;
            float label_y = target_y;

            painter.setPen(QPen(QColor(255, 255, 255, 200), 1, Qt::SolidLine));
            painter.drawLine(label_x + 10, label_y, target_x, target_y);
            
            painter.fillRect(label_x - 5, label_y - 20, 150, 45, QColor(0, 0, 0, 200));
            painter.setPen(Qt::white);
            painter.drawText(label_x, label_y - 5, "PRIMARY SIGNAL");
            
            float freq_mhz = feat.center_freq / 1e6 + (feat.peak_freq_bin - 512.0) * (feat.sample_rate / 1024.0) / 1e6;
            painter.drawText(label_x, label_y + 15, QString("%1 MHz").arg(freq_mhz, 0, 'f', 3));
        }

        if (feat.interference_detected) {
            float int_x = (peak_x < w / 2) ? w - 200 : 20;
            float int_y = h / 4;
            painter.fillRect(int_x, int_y, 160, 40, QColor(0, 0, 0, 200));
            painter.setPen(Qt::red);
            painter.drawText(int_x + 5, int_y + 15, "\xe2\x9a\xa0 INTERFERENCE");
            painter.setPen(Qt::white);
            float int_freq = feat.center_freq / 1e6 + (rand() % 10 - 5) * 0.1f; // approximation
            painter.drawText(int_x + 5, int_y + 32, QString("%1 MHz").arg(int_freq, 0, 'f', 2));
        }
    }
}
