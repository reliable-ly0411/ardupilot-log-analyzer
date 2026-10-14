QT += core gui widgets printsupport
CONFIG += c++17 release
TEMPLATE = app
TARGET = ArduPilotLogAnalyzer

SOURCES += \
    main.cpp \
    regression.cpp \
    diagnostics.cpp \
    guidance.cpp \
    mainwindow.cpp \
    parser.cpp \
    translations.cpp \
    widgets.cpp

HEADERS += \
    mainwindow.h \
    regression.h \
    diagnostics.h \
    guidance.h \
    theme.h \
    model.h \
    parser.h \
    translations.h \
    widgets.h

VERSION = 3.0.0
QMAKE_TARGET_DESCRIPTION = Flight Insight
RC_ICONS = flight.ico

