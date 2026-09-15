#pragma once

#include <QWidget>
#include <QTimer>
#include <memory>
#include <vector>
#include <complex>
#include "ConstellationDataBuffer.hpp"
#include "FeatureExtractor.hpp"

class TimePlotWidget : public QWidget {
    Q_OBJECT
public:
    TimePlotWidget(std::shared_ptr<ConstellationDataBuffer> dataBuffer,
                   std::shared_ptr<SharedFeatures> sharedFeat,
                   QWidget* parent = nullptr);
    ~TimePlotWidget() override = default;

public slots:
    void updateData();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    std::shared_ptr<ConstellationDataBuffer> m_buffer;
    std::shared_ptr<SharedFeatures> m_sharedFeat;
    QTimer m_renderTimer;
    std::vector<std::complex<float>> m_localFrame;
    QPoint m_mousePos;
};
