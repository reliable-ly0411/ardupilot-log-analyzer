#pragma once
#include <QStringList>
#include "model.h"
QStringList guideTitles();
QString guideProject(int index);
QString guideContent(int index, const DiagnosticOptions& options);
