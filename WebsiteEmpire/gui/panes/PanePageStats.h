#ifndef PANEPAGESTATSH
#define PANEPAGESTATSH

#include <QWidget>

namespace Ui {
class PanePageStats;
}

class PanePageStats : public QWidget
{
    Q_OBJECT

public:
    explicit PanePageStats(QWidget *parent = nullptr);
    ~PanePageStats();

private:
    Ui::PanePageStats *ui;
};

#endif // PANEPAGESTATSH
