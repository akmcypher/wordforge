#include "apps/desktop/appearance.hpp"
#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#endif
#include <QPalette>
#include <QWidget>
namespace wf {
namespace {
const QPalette &systemPalette() {
    // Captured before Wordforge replaces the application palette.
    static const QPalette palette = QApplication::palette();
    return palette;
}
#ifdef Q_OS_LINUX
bool portalPrefersDark() {
    auto call = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", "Read");
    call << QString("org.freedesktop.appearance") << QString("color-scheme");
    const auto reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 400);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    auto value = reply.arguments().first();
    // The portal wraps the value in one or two D-Bus variants.
    while (value.canConvert<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    return value.toUInt() == 1; // 1 = prefer dark, 2 = prefer light
}
#endif
QString settingsFile() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           "/settings.ini";
}
} // namespace

bool systemPrefersDark() {
    static const bool dark = [] {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
            return true;
#endif
#ifdef Q_OS_LINUX
        if (portalPrefersDark())
            return true;
#endif
        return systemPalette().color(QPalette::Window).lightness() < 128;
    }();
    return dark;
}
Theme savedTheme() {
    const auto value = QSettings(settingsFile(), QSettings::IniFormat)
                           .value("appearance/theme", "system")
                           .toString();
    return value == "light" ? Theme::Light : value == "dark" ? Theme::Dark : Theme::System;
}
void saveTheme(Theme theme) {
    QSettings(settingsFile(), QSettings::IniFormat)
        .setValue("appearance/theme", theme == Theme::Light  ? "light"
                                      : theme == Theme::Dark ? "dark"
                                                             : "system");
}
void applyAppearance(QWidget &window, Theme theme) {
    systemPalette();
    const bool dark = theme == Theme::Dark || (theme == Theme::System && systemPrefersDark());
    // Start from Fusion's own palette so a forced theme never keeps system colours.
    auto palette = QApplication::style()->standardPalette();
    const QColor windowColor(dark ? "#272523" : "#f5f1eb"), base(dark ? "#302d2a" : "#fffdf9"),
        alternate(dark ? "#3c3732" : "#ece6de"), text(dark ? "#eee5da" : "#3e332b"),
        muted(dark ? "#7d736a" : "#a89a8d");
    palette.setColor(QPalette::Window, windowColor);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Button, windowColor);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Light, dark ? QColor("#4a443f") : QColor("#ffffff"));
    palette.setColor(QPalette::Midlight, dark ? QColor("#3c3732") : QColor("#f0ebe4"));
    palette.setColor(QPalette::Mid, dark ? QColor("#554e48") : QColor("#cfc5ba"));
    palette.setColor(QPalette::Dark, dark ? QColor("#1d1b1a") : QColor("#a99d91"));
    palette.setColor(QPalette::Shadow, dark ? QColor("#121110") : QColor("#7a6f65"));
    palette.setColor(QPalette::Link, dark ? QColor("#d3b18c") : QColor("#805a3b"));
    palette.setColor(QPalette::Highlight, QColor("#805a3b"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    // Disabled controls must look disabled (setColor above changes every group).
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, muted);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, alternate);
    QApplication::setPalette(palette);
    window.setPalette(palette);
    window.setObjectName("wordforgeWindow");
    window.setStyleSheet(QString(R"(
        QMainWindow#wordforgeWindow { background: palette(window); }
        QMenuBar { background: palette(window); padding: 4px 8px; }
        QMenuBar::item { padding: 4px 8px; border-radius: 4px; }
        QMenuBar::item:selected { background: palette(alternate-base); }
        QToolBar { border: none; spacing: 4px; padding: 4px 12px; background: palette(window); }
        QToolButton { border: 1px solid transparent; border-radius: 5px; padding: 4px 8px; }
        QToolButton:hover { background: palette(alternate-base); }
        QToolButton:checked { background: palette(alternate-base); border-color: %3; color: palette(text); }
        QToolButton:pressed { background: palette(alternate-base); border-color: %3; }
        QToolButton:focus { border-color: %1; }
        QToolButton:disabled { color: palette(placeholder-text); }
        QToolBar#compactToolbar { padding: 4px 8px; border-bottom: 1px solid %2; }
        QToolBar#compactToolbar QToolButton { padding: 3px 5px; min-width: 22px; }
        QToolBar#compactToolbar::separator { width: 1px; background: %2; margin: 6px 6px; }
        QDockWidget#formatPanel::title { padding: 8px 12px; font-weight: 600; }
        QLabel#panelSection { color: %4; font-weight: 600; }
        QToolButton#saveButton { background: #805a3b; color: white; padding: 5px 15px; }
        QToolButton#saveButton:hover { background: #936b48; }
        QLabel#brand { color: %1; font-size: 15px; font-weight: 600; padding-right: 18px; }
        QLabel#documentState { color: palette(text); padding-right: 12px; }
        QTabWidget::pane { border: none; background: palette(base); border-radius: 6px; }
        QTabBar::tab { background: transparent; border: none; border-bottom: 2px solid transparent;
                       padding: 8px 16px; margin-right: 4px; }
        QTabBar::tab:selected { border-bottom-color: #805a3b; font-weight: 600; }
        QComboBox, QLineEdit { border: 1px solid %2; border-radius: 5px;
                               background: palette(base); padding: 4px 8px; }
        QLineEdit:focus, QComboBox:focus { border-color: %1; }
        QComboBox:disabled, QLineEdit:disabled { color: palette(placeholder-text); }
        QStatusBar { border-top: 1px solid %2; padding: 4px 12px; }
        QStatusBar::item { border: none; }
        QSlider::groove:horizontal { height: 3px; background: %2; border-radius: 1px; }
        QSlider::handle:horizontal { width: 11px; margin: -4px 0; border-radius: 5px; background: #805a3b; }
    )")
                             .arg(dark ? "#d3b18c" : "#805a3b", dark ? "#49433e" : "#ddd5cc",
                                  dark ? "#8c725c" : "#a58b73", dark ? "#c4bbb1" : "#6b5e53"));
}
} // namespace wf
