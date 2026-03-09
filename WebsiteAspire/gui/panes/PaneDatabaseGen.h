#ifndef PANEDATABASEGEN_H
#define PANEDATABASEGEN_H

#include <QWidget>

namespace Ui {
class PaneDatabaseGen;
}

class PaneDatabaseGen : public QWidget
{
    Q_OBJECT

public:
    explicit PaneDatabaseGen(QWidget *parent = nullptr);
    ~PaneDatabaseGen();

private:
    Ui::PaneDatabaseGen *ui;
};

#endif // PANEDATABASEGEN_H
