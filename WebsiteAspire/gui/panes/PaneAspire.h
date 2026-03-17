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

private slots:
    void setVisible(bool visible);

private:
    Ui::PaneAspire *ui;
    void _init();
    bool m_firstTimeVisible;
};

#endif // PANEASPIRE_H
