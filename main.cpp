#include "mainwindow.h"
#include <QApplication>
#include <QPalette>
#include <QColor>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    // 暗色主题调色板
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window,          QColor(10, 14, 20));
    darkPalette.setColor(QPalette::WindowText,      QColor(224, 230, 237));
    darkPalette.setColor(QPalette::Base,            QColor(26, 31, 46));
    darkPalette.setColor(QPalette::AlternateBase,   QColor(42, 48, 64));
    darkPalette.setColor(QPalette::Text,            QColor(224, 230, 237));
    darkPalette.setColor(QPalette::Button,          QColor(26, 31, 46));
    darkPalette.setColor(QPalette::ButtonText,      QColor(224, 230, 237));
    darkPalette.setColor(QPalette::Highlight,       QColor(0, 212, 255));
    darkPalette.setColor(QPalette::HighlightedText, QColor(10, 14, 20));
    app.setPalette(darkPalette);

    MainWindow w;
    w.show();

    return app.exec();
}
