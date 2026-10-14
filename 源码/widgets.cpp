#include "widgets.h"
#include "translations.h"

#include <QMouseEvent>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QTextStream>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const QVector<QColor>& chartColors()
{
    static const QVector<QColor> colors = {
        QColor("#346ce5"), QColor("#e6743e"), QColor("#23a085"), QColor("#ba8a24"),
        QColor("#805aa3"), QColor("#cf4867"), QColor("#4e8d3c"), QColor("#3c8795")
    };
    return colors;
}

QString compactNumber(double value)
{
    const double magnitude = std::abs(value);
    if (magnitude >= 1000) return QString::number(value, 'f', 0);
    if (magnitude >= 100) return QString::number(value, 'f', 1);
    if (magnitude >= 10) return QString::number(value, 'f', 2);
    return QString::number(value, 'f', 3);
}

QString csvValue(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

}

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(280);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
}

void PlotWidget::setReport(const LogReport* report)
{
    _report = report;
    _boundsDirty=true;
    _cursorPinned=false;
    resetView();
    update();
}

void PlotWidget::setSeriesKeys(const QStringList& keys)
{
    _keys = keys.mid(0, 8);
    updateMinimumHeight();
    _boundsDirty=true;
    _cursorPinned=false;
    resetView();
    update();
}

QStringList PlotWidget::seriesKeys() const
{
    return _keys;
}

QRectF PlotWidget::visibleBounds() const
{
    return _viewBounds;
}

bool PlotWidget::exportVisibleCsv(const QString& filePath) const
{
    if (!_report || !_viewBounds.isValid() || _keys.isEmpty()) return false;
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write("\xEF\xBB\xBF");
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << "Time_s,Time,Series,English,Unit,Value\r\n";
    for (const QString& key : _keys) {
        const Series* series = findSeries(key);
        if (!series) continue;
        for (const QPointF& point : series->points) {
            if (point.x() < _viewBounds.left() || point.x() > _viewBounds.right()) continue;
            const double relativeSeconds = point.x() - _report->flightStartUs / 1000000.0;
            stream << QString::number(relativeSeconds, 'f', 6) << ','
                   << csvValue(formatClock(static_cast<qint64>(point.x() * 1000000.0), _report->flightStartUs)) << ','
                   << csvValue(series->title) << ',' << csvValue(series->english) << ','
                   << csvValue(series->unit) << ',' << QString::number(point.y(), 'g', 15) << "\r\n";
        }
    }
    stream.flush();
    return stream.status() == QTextStream::Ok && file.commit();
}

bool PlotWidget::savePng(const QString& filePath)
{
    return grab().save(filePath, "PNG");
}

const Series* PlotWidget::findSeries(const QString& key) const
{
    if (!_report) return nullptr;
    for (const Series& series : _report->series) if (series.key == key) return &series;
    return nullptr;
}

QRectF PlotWidget::plotRect() const
{
    return QRectF(76, 22, std::max(10, width() - 102), std::max(10, height() - 80));
}

QRectF PlotWidget::calculateDataBounds() const
{
    double minX = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    if (!_report) return {};
    for (const QString& key : _keys) {
        const Series* series = findSeries(key);
        if (!series) continue;
        for (const QPointF& point : series->points) {
            if (!_fullLog && _report->flightStartUs > 0 && (point.x() * 1000000.0 < _report->flightStartUs || point.x() * 1000000.0 > _report->flightEndUs)) continue;
            if (!std::isfinite(point.x()) || !std::isfinite(point.y())) continue;
            minX = std::min(minX, point.x()); maxX = std::max(maxX, point.x());
            minY = std::min(minY, point.y()); maxY = std::max(maxY, point.y());
        }
    }
    if (!std::isfinite(minX) || !std::isfinite(minY)) return {};
    if (maxX <= minX) maxX = minX + 1;
    if (maxY <= minY) { minY -= 1; maxY += 1; }
    const double yPadding = (maxY - minY) * 0.08;
    return QRectF(minX, minY - yPadding, maxX - minX, maxY - minY + yPadding * 2);
}

void PlotWidget::ensureViewBounds()
{
    if(_boundsDirty){_dataBounds=calculateDataBounds();_boundsDirty=false;}
    if (!_dataBounds.isValid() || _dataBounds.isEmpty()) {
        _viewBounds = {};
        _viewInitialized = false;
        return;
    }
    if (!_viewInitialized) {
        _viewBounds = _dataBounds;
        _viewInitialized = true;
    } else {
        constrainView();
    }
}

void PlotWidget::resetView()
{
    _viewInitialized = false;
    _selecting = false;
    _panning = false;
    ensureViewBounds();
    _cacheValid=false;
    emit viewChanged(_viewBounds);
    update();
}

void PlotWidget::setVisibleTimeRange(qint64 centerTimeUs, double spanSeconds)
{
    ensureViewBounds();
    if (!_viewInitialized || centerTimeUs <= 0 || spanSeconds <= 0) return;
    const double center = centerTimeUs / 1000000.0;
    const double width = std::min(spanSeconds, _dataBounds.width());
    _viewBounds = QRectF(center - width / 2.0, _dataBounds.top(), width, _dataBounds.height());
    constrainView();
    _cacheValid=false;
    emit viewChanged(_viewBounds);
    update();
}

void PlotWidget::zoomIn()
{
    ensureViewBounds();
    zoomBy(0.65, 0.65, plotRect().center());
}

void PlotWidget::zoomOut()
{
    ensureViewBounds();
    zoomBy(1.55, 1.55, plotRect().center());
}

QPointF PlotWidget::pixelToData(const QPointF& pixel) const
{
    const QRectF area = plotRect();
    if (!_viewBounds.isValid() || area.width() <= 0 || area.height() <= 0) return {};
    const double xRatio = std::clamp((pixel.x() - area.left()) / area.width(), 0.0, 1.0);
    const double yRatio = std::clamp((area.bottom() - pixel.y()) / area.height(), 0.0, 1.0);
    return QPointF(_viewBounds.left() + xRatio * _viewBounds.width(),
                   _viewBounds.top() + yRatio * _viewBounds.height());
}

void PlotWidget::zoomBy(double xFactor, double yFactor, const QPointF& anchorPixel)
{
    if (!_viewInitialized || !_viewBounds.isValid()) return;
    const QPointF anchor = pixelToData(anchorPixel);
    const double xRatio = (_viewBounds.width() > 0) ? (anchor.x() - _viewBounds.left()) / _viewBounds.width() : 0.5;
    const double yRatio = (_viewBounds.height() > 0) ? (anchor.y() - _viewBounds.top()) / _viewBounds.height() : 0.5;
    const double width = _viewBounds.width() * xFactor;
    const double height = _viewBounds.height() * yFactor;
    _viewBounds = QRectF(anchor.x() - width * xRatio, anchor.y() - height * yRatio, width, height);
    constrainView();
    _cacheValid=false;
    emit viewChanged(_viewBounds);
    update();
}

void PlotWidget::constrainView()
{
    if (!_dataBounds.isValid() || !_viewBounds.isValid()) return;
    const double minimumWidth = std::max(_dataBounds.width() / 10000.0, 0.001);
    const double minimumHeight = std::max(_dataBounds.height() / 10000.0, 1e-9);
    double width = std::clamp(_viewBounds.width(), minimumWidth, _dataBounds.width());
    double height = std::clamp(_viewBounds.height(), minimumHeight, _dataBounds.height());
    double left = _viewBounds.left();
    double top = _viewBounds.top();
    if (left < _dataBounds.left()) left = _dataBounds.left();
    if (left + width > _dataBounds.right()) left = _dataBounds.right() - width;
    if (top < _dataBounds.top()) top = _dataBounds.top();
    if (top + height > _dataBounds.bottom()) top = _dataBounds.bottom() - height;
    _viewBounds = QRectF(left, top, width, height);
}

void PlotWidget::panByPixels(const QPoint& delta)
{
    const QRectF area = plotRect();
    if (!_viewInitialized || area.width() <= 0 || area.height() <= 0) return;
    _viewBounds.translate(-delta.x() / area.width() * _viewBounds.width(),
                          delta.y() / area.height() * _viewBounds.height());
    constrainView();
    _cacheValid=false;
    emit viewChanged(_viewBounds);
    update();
}

void PlotWidget::updateMinimumHeight()
{
    QSet<QString> groups;
    for(const QString& key:_keys){const Series* series=findSeries(key);if(series && !series->points.isEmpty())groups.insert(_independentAxes?(series->unit.isEmpty()?key:series->unit):QStringLiteral("shared"));}
    setMinimumHeight(std::max(280,groups.size()*76+80));
}
void PlotWidget::setIndependentAxes(bool enabled)
{
    _independentAxes=enabled;updateMinimumHeight();_cacheValid=false;update();
}
void PlotWidget::setFullLog(bool enabled)
{
    _fullLog=enabled;_boundsDirty=true;resetView();
}
void PlotWidget::setTimeWindow(double start,double end)
{
    ensureViewBounds();if(!_viewInitialized || !std::isfinite(start) || !std::isfinite(end) || end<=start)return;
    _viewBounds=QRectF(start,_dataBounds.top(),end-start,_dataBounds.height());constrainView();_cacheValid=false;emit viewChanged(_viewBounds);update();
}
QPointF PlotWidget::nearestSample(const QString& key,double time) const
{
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const Series* s=findSeries(key);if(!s || s->points.isEmpty())return {nan,nan};
    auto it=std::lower_bound(s->points.cbegin(),s->points.cend(),time,[](const QPointF& p,double t){return p.x()<t;});
    if(it==s->points.cend())--it;
    else if(it!=s->points.cbegin() && std::abs((it-1)->x()-time)<=std::abs(it->x()-time))--it;
    if(std::abs(it->x()-time)>2.0)return {nan,nan};
    return *it;
}
void PlotWidget::resizeEvent(QResizeEvent* event)
{
    _cacheValid=false;QWidget::resizeEvent(event);
}
void PlotWidget::renderGraph()
{
    _graphCache=QPixmap(size()*devicePixelRatioF());_graphCache.setDevicePixelRatio(devicePixelRatioF());_graphCache.fill(Qt::white);
    QPainter painter(&_graphCache);painter.setRenderHint(QPainter::Antialiasing);_lanes.clear();
    const QRectF all=plotRect();
    if(!_report || _keys.isEmpty() || !_viewBounds.isValid()){
        painter.setPen(QColor("#8996ab"));painter.drawText(rect(),Qt::AlignCenter,QStringLiteral("选择分析项目或勾选数据字段\n\n不同单位分轴显示 · 拖动框选 · 滚轮缩放 · 点击锁定读数"));_cacheValid=true;return;
    }
    const double minX=_viewBounds.left(),maxX=_viewBounds.right();
    for(const QString& key:_keys){
        const Series* s=findSeries(key);if(!s || s->points.isEmpty())continue;
        const QString group=!_independentAxes ? "shared" : s->unit.isEmpty()?key:s->unit;
        int index=-1;
        for(int i=0;i<_lanes.size();++i)if(_lanes[i].unit==group){index=i;break;}
        if(index<0){Lane lane;lane.unit=group;lane.minimum=std::numeric_limits<double>::infinity();lane.maximum=-lane.minimum;_lanes.append(lane);index=_lanes.size()-1;}
        _lanes[index].keys.append(key);
    }
    if(_lanes.isEmpty()){_cacheValid=true;return;}
    const double laneHeight=all.height()/_lanes.size();
    for(int laneIndex=0;laneIndex<_lanes.size();++laneIndex){
        Lane& lane=_lanes[laneIndex];
        lane.area=QRectF(all.left(),all.top()+laneIndex*laneHeight+18,all.width(),std::max(10.0,laneHeight-25));
        for(const QString& key:lane.keys){
            const Series* s=findSeries(key);
            auto begin=std::lower_bound(s->points.cbegin(),s->points.cend(),minX,[](const QPointF& p,double t){return p.x()<t;});
            auto end=std::upper_bound(s->points.cbegin(),s->points.cend(),maxX,[](double t,const QPointF& p){return t<p.x();});
            for(auto it=begin;it!=end;++it)if(std::isfinite(it->y())){lane.minimum=std::min(lane.minimum,it->y());lane.maximum=std::max(lane.maximum,it->y());}
        }
        if(!_independentAxes){lane.minimum=_viewBounds.top();lane.maximum=_viewBounds.bottom();}
        else if(!std::isfinite(lane.minimum)){lane.minimum=0;lane.maximum=1;}
        else {if(lane.maximum<=lane.minimum){lane.minimum-=1;lane.maximum+=1;}const double pad=(lane.maximum-lane.minimum)*0.08;lane.minimum-=pad;lane.maximum+=pad;}
        const QRectF area=lane.area;
        painter.fillRect(area,QColor("#fafbfe"));
        painter.setPen(QPen(QColor("#e7edf6"),1));
        for(int i=0;i<=2;++i){
            const double y=area.top()+area.height()*i/2; painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
            painter.setPen(QColor("#8794a8"));painter.drawText(QRectF(0,y-8,all.left()-9,16),Qt::AlignRight|Qt::AlignVCenter,compactNumber(lane.maximum-(lane.maximum-lane.minimum)*i/2));painter.setPen(QPen(QColor("#e7edf6"),1));
        }
        for(int i=0;i<=6;++i){const double x=area.left()+area.width()*i/6;painter.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));}
        int legendX=qRound(area.left());
        for(const QString& key:lane.keys){
            const Series* series=findSeries(key);const int colorIndex=_keys.indexOf(key);const QColor color=chartColors()[colorIndex%chartColors().size()];
            const int labelWidth=qRound(area.width()/lane.keys.size());
            painter.setPen(color);painter.drawText(QRectF(legendX,area.top()-19,labelWidth-8,18),Qt::AlignLeft|Qt::AlignVCenter,painter.fontMetrics().elidedText(series->english+(series->unit.isEmpty()?QStringLiteral(" [原始单位]"):" ["+series->unit+"]"),Qt::ElideRight,labelWidth-8));legendX+=labelWidth;
            auto begin=std::lower_bound(series->points.cbegin(),series->points.cend(),minX,[](const QPointF& p,double t){return p.x()<t;});
            auto end=std::upper_bound(series->points.cbegin(),series->points.cend(),maxX,[](double t,const QPointF& p){return t<p.x();});
            if(begin!=series->points.cbegin())--begin;
            if(end!=series->points.cend())++end;
            QPainterPath path;bool started=false;double lastTime=-1;
            auto drawPoint=[&](const QPointF& p){
                const double x=area.left()+(p.x()-minX)/(maxX-minX)*area.width(),y=area.bottom()-(p.y()-lane.minimum)/(lane.maximum-lane.minimum)*area.height();
                if(!started || (lastTime>=0 && p.x()-lastTime>2.0)){path.moveTo(x,y);started=true;}else path.lineTo(x,y);lastTime=p.x();
            };
            // Retain first/last/min/max within each screen column. Diagnostics and CSV use every sample.
            auto it=begin;
            while(it!=end){
                const int bucket=qFloor((it->x()-minX)/(maxX-minX)*area.width());
                auto first=it,last=it,minimum=it,maximum=it;
                while(it!=end && qFloor((it->x()-minX)/(maxX-minX)*area.width())==bucket){last=it;if(it->y()<minimum->y())minimum=it;if(it->y()>maximum->y())maximum=it;++it;}
                QVector<decltype(first)> extremes{first,minimum,maximum,last};std::sort(extremes.begin(),extremes.end());
                auto previous=end;for(auto point:extremes){if(point!=previous)drawPoint(*point);previous=point;}
            }
            painter.save();painter.setClipRect(area);painter.setPen(QPen(color,1.6));painter.drawPath(path);
            if(std::distance(begin,end)==1){const QPointF p=*begin;painter.setBrush(color);painter.drawEllipse(QPointF(area.left()+(p.x()-minX)/(maxX-minX)*area.width(),area.bottom()-(p.y()-lane.minimum)/(lane.maximum-lane.minimum)*area.height()),2.5,2.5);}
            painter.restore();
        }
    }
    // Marks are evidence locations, not a claim that every mode change is a fault.
    for(const Finding& f:_report->findings){
        if(f.timeUs<=0 || f.severity<Severity::Notice)continue;
        const double t=f.timeUs/1e6;
        if(t<minX || t>maxX)continue;
        const double x=all.left()+(t-minX)/(maxX-minX)*all.width();
        const QColor c=f.severity>=Severity::Warning?QColor("#dd6950"):QColor("#b89749");painter.setPen(QPen(c,1,Qt::DashLine));painter.drawLine(QPointF(x,all.top()),QPointF(x,all.bottom()));
        painter.setBrush(c);painter.setPen(Qt::NoPen);painter.drawEllipse(QPointF(x,all.top()-6),3,3);
    }
    for(int i=0;i<=6;++i){const double x=all.left()+all.width()*i/6;const qint64 t=qRound64((minX+(maxX-minX)*i/6)*1e6);painter.setPen(QColor("#7b8aa0"));painter.drawText(QRectF(x-42,all.bottom()+7,84,20),Qt::AlignCenter,formatClock(t,_report->flightStartUs));}
    painter.setPen(QColor("#8c99ad"));painter.drawText(QRectF(all.left(),height()-22,all.width(),18),Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("相对解锁时间  ·  点击锁定读数 / Esc 解除  ·  框选区间  ·  滚轮缩放  ·  右键平移  ·  双击复位"));
    _cacheValid=true;
}
void PlotWidget::paintEvent(QPaintEvent*)
{
    ensureViewBounds();if(!_cacheValid || _graphCache.size()!=size()*devicePixelRatioF())renderGraph();
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.drawPixmap(0,0,_graphCache);
    const QRectF all=plotRect();
    if(_report && _viewBounds.isValid() && !_selecting && !_panning && (_cursorPinned || (_mouseInside && all.contains(_mousePosition)))){
        const double t=_cursorPinned?_probeTime:pixelToData(_mousePosition).x();
        const double x=all.left()+(t-_viewBounds.left())/_viewBounds.width()*all.width();
        if(x<all.left() || x>all.right())return;
        painter.setPen(QPen(QColor("#778baa"),1,Qt::DashLine));painter.drawLine(QPointF(x,all.top()),QPointF(x,all.bottom()));
        QStringList lines;lines<<formatClock(qRound64(t*1e6),_report->flightStartUs,true)+(_cursorPinned?QStringLiteral(" · 已锁定"):QStringLiteral(" · 点击锁定"));
        for(const QString& key:_keys){
            const Series* s=findSeries(key);if(!s)continue;const QPointF sample=nearestSample(key,t);
            if(!std::isfinite(sample.y())){lines<<s->english+QStringLiteral("  无近邻样本");continue;}
            lines<<QStringLiteral("%1  %2 %3  (Δt=%4 s)").arg(s->english,compactNumber(sample.y()),s->unit).arg(sample.x()-t,0,'f',3);
            for(const Lane& lane:_lanes)if(lane.keys.contains(key)){
                const double pointX=lane.area.left()+(sample.x()-_viewBounds.left())/_viewBounds.width()*lane.area.width(),pointY=lane.area.bottom()-(sample.y()-lane.minimum)/(lane.maximum-lane.minimum)*lane.area.height();
                if(lane.area.contains(QPointF(pointX,pointY))){painter.setPen(QPen(Qt::white,2));painter.setBrush(chartColors()[_keys.indexOf(key)%chartColors().size()]);painter.drawEllipse(QPointF(pointX,pointY),3.5,3.5);}break;
            }
        }
        const int tooltipWidth=std::min(420,width()-20),tooltipHeight=lines.size()*21+18;
        const double left=std::clamp(x+14,8.0,double(std::max(8,width()-tooltipWidth-8)));
        QRectF box(left,all.top()+12,tooltipWidth,tooltipHeight);painter.setPen(QColor("#d6e0ef"));painter.setBrush(QColor(255,255,255,246));painter.drawRoundedRect(box,6,6);painter.setPen(QColor("#344862"));
        for(int i=0;i<lines.size();++i)painter.drawText(box.adjusted(10,8+i*21,-10,0),Qt::AlignLeft|Qt::AlignTop,painter.fontMetrics().elidedText(lines[i],Qt::ElideRight,tooltipWidth-20));
    }
    if(_selecting){QRectF selection=QRectF(_dragStart,_dragCurrent).normalized().intersected(all);if(_independentAxes)selection=QRectF(selection.left(),all.top(),selection.width(),all.height());painter.setPen(QPen(QColor("#346ce5"),1,Qt::DashLine));painter.setBrush(QColor(52,108,229,30));painter.drawRect(selection);}
}

void PlotWidget::mousePressEvent(QMouseEvent* event)
{
    ensureViewBounds();
    if (!plotRect().contains(event->pos()) || !_viewInitialized) return;
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::RightButton || (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier))) {
        _panning = true;
        _lastPanPosition = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        _selecting = true;
        _dragStart = _dragCurrent = event->pos();
        setCursor(Qt::CrossCursor);
        event->accept();
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    _mousePosition = event->pos();
    _mouseInside = true;
    if (_panning) {
        const QPoint delta = event->pos() - _lastPanPosition;
        _lastPanPosition = event->pos();
        panByPixels(delta);
        event->accept();
        return;
    }
    if (_selecting) {
        _dragCurrent = event->pos();
        update();
        event->accept();
        return;
    }
    setCursor(plotRect().contains(event->pos()) ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void PlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (_panning && (event->button() == Qt::RightButton || event->button() == Qt::LeftButton)) {
        _panning = false;
        setCursor(Qt::CrossCursor);
        update();
        event->accept();
        return;
    }
    if (_selecting && event->button() == Qt::LeftButton) {
        _dragCurrent = event->pos();
        const QRectF pixels = QRectF(_dragStart, _dragCurrent).normalized().intersected(plotRect());
        _selecting = false;
        if (pixels.width() >= 10 && (_independentAxes || pixels.height() >= 10)) {
            const QPointF bottomLeft = pixelToData(QPointF(pixels.left(), pixels.bottom()));
            const QPointF topRight = pixelToData(QPointF(pixels.right(), pixels.top()));
            _viewBounds = QRectF(bottomLeft.x(), _independentAxes?_dataBounds.top():bottomLeft.y(), topRight.x() - bottomLeft.x(), _independentAxes?_dataBounds.height():topRight.y() - bottomLeft.y());
            constrainView();
            _cacheValid=false;
    emit viewChanged(_viewBounds);
        }
        if(pixels.width()<10 && pixels.height()<10){_cursorPinned=!_cursorPinned;_probeTime=pixelToData(event->pos()).x();}
        setCursor(Qt::CrossCursor);
        update();
        event->accept();
    }
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && plotRect().contains(event->pos())) {
        _cursorPinned=false;
        resetView();
        event->accept();
    }
}

void PlotWidget::wheelEvent(QWheelEvent* event)
{
    if (!plotRect().contains(event->position()) || !_viewInitialized) return;
    const double factor = event->angleDelta().y() > 0 ? 0.78 : 1.28;
    const bool horizontalOnly = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool verticalOnly = event->modifiers().testFlag(Qt::ControlModifier);
    zoomBy(verticalOnly ? 1.0 : factor, horizontalOnly ? 1.0 : factor, event->position());
    event->accept();
}

void PlotWidget::keyPressEvent(QKeyEvent* event)
{
    if(event->key()==Qt::Key_Escape){_cursorPinned=false;update();event->accept();return;}
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) zoomIn();
    else if (event->key() == Qt::Key_Minus) zoomOut();
    else if (event->key() == Qt::Key_R || event->key() == Qt::Key_Home) resetView();
    else { QWidget::keyPressEvent(event); return; }
    event->accept();
}

void PlotWidget::leaveEvent(QEvent*)
{
    _mouseInside = false;
    if (!_selecting && !_panning) unsetCursor();
    update();
}

RouteWidget::RouteWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(420);
}

void RouteWidget::setReport(const LogReport* report)
{
    _report = report;
    update();
}

void RouteWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#eef3f0"));
    painter.setPen(QPen(QColor("#dde6e2"), 1));
    for (int x = 0; x < width(); x += 40) painter.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += 40) painter.drawLine(0, y, width(), y);

    if (!_report || _report->route.size() < 2) {
        painter.setPen(QColor("#718086"));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("日志中没有足够的有效位置数据"));
        return;
    }
    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    for (const GeoSample& point : _report->route) {
        minLat = std::min(minLat, point.latitude); maxLat = std::max(maxLat, point.latitude);
        minLon = std::min(minLon, point.longitude); maxLon = std::max(maxLon, point.longitude);
    }
    const double centerLat = (minLat + maxLat) / 2.0;
    const double lonScale = std::max(0.1, std::cos(centerLat * 3.14159265358979323846 / 180.0));
    const double widthDegrees = std::max(1e-7, (maxLon - minLon) * lonScale);
    const double heightDegrees = std::max(1e-7, maxLat - minLat);
    const QRectF area = rect().adjusted(38, 28, -38, -42);
    const double scale = std::min(area.width() / widthDegrees, area.height() / heightDegrees);
    auto project = [&](const GeoSample& point) {
        const double x = area.center().x() + (point.longitude - (minLon + maxLon) / 2.0) * lonScale * scale;
        const double y = area.center().y() - (point.latitude - centerLat) * scale;
        return QPointF(x, y);
    };

    QPainterPath routePath;
    routePath.moveTo(project(_report->route.first()));
    for (int index = 1; index < _report->route.size(); ++index) routePath.lineTo(project(_report->route.at(index)));
    painter.setPen(QPen(QColor("#1f8f7a"), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(routePath);

    const QPointF start = project(_report->route.first());
    const QPointF end = project(_report->route.last());
    painter.setPen(Qt::NoPen); painter.setBrush(QColor("#2372a8")); painter.drawEllipse(start, 7, 7);
    painter.setBrush(QColor("#e36a4a")); painter.drawEllipse(end, 7, 7);
    painter.setPen(QColor("#26373d"));
    painter.drawText(QRectF(start.x() + 10, start.y() - 12, 80, 24), QStringLiteral("起点"));
    painter.drawText(QRectF(end.x() + 10, end.y() - 12, 80, 24), QStringLiteral("终点"));

    painter.setPen(QColor("#53666c"));
    painter.drawText(QRectF(16, height() - 32, width() - 32, 22), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("轨迹点 %1 · 飞行距离约 %2 km · 离线轨迹视图").arg(_report->route.size()).arg(_report->distanceMeters / 1000.0, 0, 'f', 2));
}

DropPanel::DropPanel(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(300);
    setCursor(Qt::PointingHandCursor);
}

void DropPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#bccde8"),1.5,Qt::DashLine));p.setBrush(QColor("#edf3ff"));p.drawRoundedRect(rect().adjusted(2,2,-2,-2),12,12);
    const double cy=height()/2.0;
    p.setPen(Qt::NoPen);p.setBrush(QColor("#dbe8ff"));p.drawRoundedRect(QRectF(width()/2.0-27,cy-76,54,54),12,12);
    p.setPen(QPen(QColor("#346ce5"),2.5));QPainterPath path;path.moveTo(width()/2.0-16,cy-49);path.lineTo(width()/2.0-8,cy-49);path.lineTo(width()/2.0-3,cy-62);path.lineTo(width()/2.0+4,cy-37);path.lineTo(width()/2.0+9,cy-49);path.lineTo(width()/2.0+17,cy-49);p.drawPath(path);
    QFont title=font();title.setPixelSize(22);title.setBold(true);p.setFont(title);p.setPen(QColor("#254978"));p.drawText(QRectF(10,cy-5,width()-20,34),Qt::AlignCenter,QStringLiteral("拖入 .BIN，开始试飞复盘"));
    QFont helper=font();helper.setPixelSize(12);helper.setBold(false);p.setFont(helper);p.setPen(QColor("#7188a9"));p.drawText(QRectF(10,cy+36,width()-20,44),Qt::AlignCenter,QStringLiteral("或点击这里选择日志 · 自动识别机型，扫描异常并定位证据\n首次使用可打开左侧教学示例，跟随分析引导练习"));
}

void DropPanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) emit browseRequested();
}
