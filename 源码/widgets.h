#pragma once

#include "model.h"

#include <QRectF>
#include <QWidget>
#include <QPixmap>

class PlotWidget final : public QWidget {
    Q_OBJECT
public:
    explicit PlotWidget(QWidget* parent = nullptr);
    void setReport(const LogReport* report);
    void setSeriesKeys(const QStringList& keys);
    QStringList seriesKeys() const;
    QRectF visibleBounds() const;
    bool exportVisibleCsv(const QString& filePath) const;
    bool savePng(const QString& filePath);
    void setIndependentAxes(bool enabled);
    void setFullLog(bool enabled);
    void setTimeWindow(double startSeconds, double endSeconds);
    QPointF nearestSample(const QString& key, double timeSeconds) const;

public slots:
    void zoomIn();
    void zoomOut();
    void resetView();
    void setVisibleTimeRange(qint64 centerTimeUs, double spanSeconds = 120.0);

signals:
    void viewChanged(const QRectF& bounds);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Lane { QStringList keys; QString unit; double minimum=0, maximum=1; QRectF area; };
    void renderGraph();
    void updateMinimumHeight();
    QVector<Lane> _lanes;
    QPixmap _graphCache;
    bool _cacheValid=false;
    bool _boundsDirty=true;
    bool _independentAxes=true;
    bool _fullLog=false;
    bool _cursorPinned=false;
    double _probeTime=0;
    const Series* findSeries(const QString& key) const;
    QRectF plotRect() const;
    QRectF calculateDataBounds() const;
    void ensureViewBounds();
    void zoomBy(double xFactor, double yFactor, const QPointF& anchorPixel);
    void panByPixels(const QPoint& delta);
    void constrainView();
    QPointF pixelToData(const QPointF& pixel) const;
    const LogReport* _report = nullptr;
    QStringList _keys;
    QPoint _mousePosition;
    QPoint _dragStart;
    QPoint _dragCurrent;
    QPoint _lastPanPosition;
    QRectF _dataBounds;
    QRectF _viewBounds;
    bool _mouseInside = false;
    bool _selecting = false;
    bool _panning = false;
    bool _viewInitialized = false;
};

class RouteWidget final : public QWidget {
    Q_OBJECT
public:
    explicit RouteWidget(QWidget* parent = nullptr);
    void setReport(const LogReport* report);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const LogReport* _report = nullptr;
};

class DropPanel final : public QWidget {
    Q_OBJECT
public:
    explicit DropPanel(QWidget* parent = nullptr);

signals:
    void browseRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
};
