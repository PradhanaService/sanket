#pragma once

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPushButton>
#include <memory>
#include "WaterfallDataBuffer.hpp"
#include "FeatureExtractor.hpp"

class WaterfallPlotWidget : public QWidget {
    Q_OBJECT
public:
    WaterfallPlotWidget(std::shared_ptr<WaterfallDataBuffer> dataBuffer,
                        std::shared_ptr<SharedFeatures> sharedFeat,
                        QWidget* parent = nullptr);
    ~WaterfallPlotWidget() override = default;

public slots:
    void updateData();
    void play();
    void pause();
    void jumpToLive();
    void jumpToHistory();
    void setScrollOffset(int offset);

signals:
    void scrollOffsetChanged(int offset);
    void maxHistoryChanged(int maxOffset);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::shared_ptr<WaterfallDataBuffer> m_buffer;
    std::shared_ptr<SharedFeatures> m_sharedFeat;
    QTimer m_renderTimer;
    QImage m_historyImage;
    std::vector<WaterfallDataBuffer::Row> m_localRows;
    std::vector<QRgb> m_colormap;
    QPoint m_mousePos;
    bool m_isPaused = false;
    int m_scrollOffset = 0;
    int m_maxHistoryRows = 4096;
    int m_currentRowCount = 0;
    bool m_isDragging = false;
    int m_dragStartY = 0;
    int m_dragStartOffset = 0;
};
