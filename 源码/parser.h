#pragma once

#include "model.h"

#include <QObject>
#include <atomic>

class LogParser final : public QObject {
    Q_OBJECT
public:
    explicit LogParser(QObject* parent = nullptr);
    void setDiagnosticOptions(const DiagnosticOptions& options) { _options = options; }

public slots:
    void parse(const QString& filePath);
    void cancel();

signals:
    void progress(int percent, const QString& phase);
    void finished(const LogReport& report);

private:
    std::atomic_bool _cancelled{false};
    DiagnosticOptions _options;
};

