#pragma once
#include <QString>
inline QString flightTheme() { return QStringLiteral(R"(
* { color: #243248; font-family: "Microsoft YaHei UI"; }
QMainWindow { background: #f3f5f9; }
#sidebar { background: #152138; }
#brandKicker { color: #71a5fc; font-size: 10px; font-weight: 700; }
#brandTitle { color: white; font-size: 30px; font-weight: 700; }
#brandSubtitle { color: #a5b4ce; font-size: 11px; }
#supportText { color: #8293b0; font-size: 11px; }
#navigation { background: transparent; border: 0; outline: 0; color: #bac8dd; font-size: 13px; }
#navigation::item { height: 39px; border-radius: 7px; padding-left: 12px; margin: 3px 0; }
#navigation::item:selected { background: #2c5fb7; color: white; font-weight: 700; }
#navigation::item:hover:!selected { background: #23334d; }
#pageTitle { font-size: 21px; font-weight: 700; }
#pageSubtitle { color: #738096; font-size: 11px; }
#statusPill { background: #e4ecfc; color: #315dab; border-radius: 12px; padding: 4px 10px; font-weight: 700; font-size: 11px; }
#primaryButton, #guideCurveButton { background: #346ce5; color: white; border: 0; border-radius: 6px; padding: 9px 16px; font-weight: 700; }
#primaryButton:hover, #guideCurveButton:hover { background: #265aca; }
#secondaryButton, QToolButton { background: white; border: 1px solid #d8e0ed; border-radius: 6px; padding: 7px 12px; }
#secondaryButton:hover, QToolButton:hover { background: #edf3ff; border-color: #8eaee8; }
QPushButton:disabled, QToolButton:disabled { color: #a1aabc; background: #edf0f5; }
#sidebarButton { background: #23334d; color: #ccdaf0; border: 1px solid #354864; border-radius: 6px; padding: 9px; text-align: left; }
#sidebarButton:hover { background: #2c4161; }
#panel, #metricCard { background: white; border: 1px solid #e0e5ef; border-radius: 8px; }
#overviewBanner { background: #eaf1ff; border-left: 4px solid #346ce5; border-radius: 5px; padding: 12px 14px; color: #305488; font-size: 12px; }
#sectionTitle { font-size: 14px; font-weight: 700; }
#helperText { color: #748299; font-size: 11px; }
#analysisInsight { background: #f0f5ff; color: #3f597d; border-left: 3px solid #346ce5; padding: 8px 10px; font-size: 11px; }
#metricName { color: #718096; font-size: 11px; }
#metricValue { font-size: 21px; font-weight: 700; color: #203a63; }
#metricDetail { color: #8995a8; font-size: 10px; }
QLineEdit, QComboBox, QDoubleSpinBox, QTextEdit, QTextBrowser { background: white; border: 1px solid #d8e0ed; border-radius: 5px; padding: 6px 8px; selection-background-color: #346ce5; }
QLineEdit:focus, QComboBox:focus, QTextEdit:focus { border-color: #6c9bea; }
QTreeWidget, QTableWidget, #recentList { background: white; alternate-background-color: #f8faff; border: 1px solid #e0e5ef; border-radius: 6px; outline: 0; selection-background-color: #e4edfd; selection-color: #254c8c; }
QTreeWidget::item { height: 27px; }
#guideTopics { background: white; border: 1px solid #e0e5ef; border-radius: 6px; font-size: 13px; outline: 0; }
#guideTopics::item { height: 38px; padding-left: 10px; }
#guideTopics::item:selected { background: #e4edfd; color: #254c8c; }
QHeaderView::section { background: #f1f4fa; color: #667790; border: 0; border-bottom: 1px solid #e0e5ef; padding: 8px; font-weight: 700; }
QTableWidget::item { padding: 5px; border-bottom: 1px solid #eff2f7; }
QMenu { background: white; border: 1px solid #d8e0ed; padding: 5px; }
QMenu::item { padding: 8px 22px; }
QMenu::item:selected { background: #e4edfd; }
QProgressBar { border: 0; background: #e1e7f2; border-radius: 3px; }
QProgressBar::chunk { background: #346ce5; border-radius: 3px; }
QScrollArea { background: transparent; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }
QScrollBar::handle:vertical { background: #bec9db; border-radius: 3px; min-height: 25px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QSplitter::handle { background: #e2e8f2; width: 2px; }
QToolTip { background: white; color: #243248; border: 1px solid #d8e0ed; padding: 6px; }
#chartToolButton { padding: 0; font-size: 16px; font-weight: 700; }
QStatusBar { background: #e9edf5; color: #677892; font-size: 11px; }
)"); }
