#pragma once

#include <QWidget>
#include <QTimer>
#include <QImage>
#include <vector>
#include <complex>
#include <memory>
#include "ConstellationDataBuffer.hpp"
#include "FeatureExtractor.hpp"

/**
 * @class ConstellationPlotWidget
 * @brief A standalone Qt Widget that renders a real-time constellation scatter plot.
 * 
 * Maps I/Q complex values to X/Y coordinates. Implements density mapping natively 
 * by drawing transparent (alpha-blended) points, causing modulation clusters (QPSK, QAM) 
 * to visually "glow" over the background.
 */
class ConstellationPlotWidget : public QWidget {
    Q_OBJECT
public:
    ConstellationPlotWidget(std::shared_ptr<ConstellationDataBuffer> dataBuffer,
                            std::shared_ptr<SharedFeatures> sharedFeat,
                            QWidget* parent = nullptr);
    ~ConstellationPlotWidget() override = default;

public slots:
    void updateData();
    void play();
    void pause();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    std::shared_ptr<ConstellationDataBuffer> m_buffer;
    std::shared_ptr<SharedFeatures> m_sharedFeat;
    std::vector<std::complex<float>> m_localFrame; // Pre-allocated consumer buffer
    QTimer m_renderTimer;
    QImage m_decayBuffer;
    QPoint m_mousePos;

    // Coordinate mapping helper
    int mapToPixel(float value, float min_val, float max_val, int pixel_range);
};
