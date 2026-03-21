#ifndef PANEDATABASEGEN_H
#define PANEDATABASEGEN_H

#include <QWidget>

namespace Ui {
class PaneDatabaseGen;
}

// Container pane that mirrors PaneAspire but for AbstractGenerator instances.
// A sidebar QListWidget lists all registered generator names; the matching
// WidgetGenerator is shown in the QStackedWidget on the right.
class PaneDatabaseGen : public QWidget
{
    Q_OBJECT

public:
    explicit PaneDatabaseGen(QWidget *parent = nullptr);
    ~PaneDatabaseGen();

private:
    Ui::PaneDatabaseGen *ui;
    void _init();
};

#endif // PANEDATABASEGEN_H
