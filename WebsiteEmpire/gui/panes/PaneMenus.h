#ifndef PANEMENUS_H
#define PANEMENUS_H

#include <QWidget>

namespace Ui {
class PaneMenus;
}

class PaneMenus : public QWidget
{
    Q_OBJECT

public:
    explicit PaneMenus(QWidget *parent = nullptr);
    ~PaneMenus();

private:
    Ui::PaneMenus *ui;
};

#endif // PANEMENUS_H
