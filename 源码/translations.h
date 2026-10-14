#pragma once

#include "model.h"

#include <QString>

QString parameterChineseName(const QString& name);
QString parameterCategory(const QString& name);
QString messageChineseName(const QString& name);
QString fieldChineseName(const QString& message, const QString& field);
QString modeName(VehicleKind kind, int mode);
QString qposStateName(int state);
QString formatDuration(qint64 usec);
QString formatClock(qint64 timeUs, qint64 originUs, bool milliseconds = false);
QString formatFileSize(qint64 bytes);

