#ifndef DIALOGPICKENGINE_H
#define DIALOGPICKENGINE_H

#include <QDialog>

namespace Ui {
class DialogPickEngine;
}

// Modal dialog that lets the user pick an AbstractEngine for the current
// working directory.
//
// On acceptance the chosen engine's getId() is written to settings.ini under
// settingsKey().  The dialog must only be shown when that key is absent —
// see main.cpp.  Once written, the engine cannot be changed (the key is
// permanent for the lifetime of the working directory).
class DialogPickEngine : public QDialog
{
    Q_OBJECT

public:
    explicit DialogPickEngine(QWidget *parent = nullptr);
    ~DialogPickEngine();

    // QSettings key used to persist the chosen engine id in settings.ini.
    static QString settingsKey();

private slots:
    void onItemSelectionChanged();
    void onAccepted();

private:
    Ui::DialogPickEngine *ui;
};

#endif // DIALOGPICKENGINE_H
