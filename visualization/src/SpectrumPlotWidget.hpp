#pragma once

#include <QWidget>
#include <QTimer>
#include <memory>
#include <vector>
#include <complex>
#include "WaterfallDataBuffer.hpp"
#include "ConstellationDataBuffer.hpp"
#include "FeatureExtractor.hpp"

class SpectrumPlotWidget : public QWidget {
    Q_OBJECT
public:
    SpectrumPlotWidget(std::shared_ptr<WaterfallDataBuffer> waterBuffer,
                       std::shared_ptr<ConstellationDataBuffer> constelBuffer,
                       std::shared_ptr<SharedFeatures> sharedFeat,
                       QWidget* parent = nullptr);
    ~SpectrumPlotWidget() override = default;

public slots:
    void updateData();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    std::shared_ptr<WaterfallDataBuffer> m_waterBuffer;
    std::shared_ptr<ConstellationDataBuffer> m_constelBuffer;
    std::shared_ptr<SharedFeatures> m_sharedFeat;
    QTimer m_renderTimer;
    std::vector<float> m_latestSpectrumRow;
    std::vector<std::complex<float>> m_latestTimeRow;
    QPoint m_mousePos;
};
