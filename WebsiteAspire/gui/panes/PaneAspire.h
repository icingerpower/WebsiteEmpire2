#ifndef PANEASPIRE_H
#define PANEASPIRE_H

#include <QWidget>

namespace Ui {
class PaneAspire;
}

class PaneAspire : public QWidget
{
    Q_OBJECT

public:
    explicit PaneAspire(QWidget *parent = nullptr);
    ~PaneAspire();

private:
    Ui::PaneAspire *ui;
};

#endif // PANEASPIRE_H
