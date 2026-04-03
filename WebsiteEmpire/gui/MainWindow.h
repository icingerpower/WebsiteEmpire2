#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QScopedPointer>

#include "website/HostTable.h"

class AbstractEngine;
class WebsiteSettingsTable;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void _init();

    Ui::MainWindow                      *ui;
    QScopedPointer<HostTable>            m_hostTable;
    QScopedPointer<AbstractEngine>       m_engine;
    QScopedPointer<WebsiteSettingsTable> m_settingsTable;
};
#endif // MAINWINDOW_H
