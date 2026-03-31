#ifndef DIALOGEDITHOSTS_H
#define DIALOGEDITHOSTS_H

#include <QDialog>

class HostTable;

namespace Ui {
class DialogEditHosts;
}

// Modal dialog for viewing and editing the HostTable.
// Changes are auto-saved by HostTable; no explicit save step is needed.
class DialogEditHosts : public QDialog
{
    Q_OBJECT

public:
    explicit DialogEditHosts(HostTable *hostTable, QWidget *parent = nullptr);
    ~DialogEditHosts();

private slots:
    void addRow();
    void removeRow();

private:
    Ui::DialogEditHosts *ui;
    HostTable           *m_hostTable;
};

#endif // DIALOGEDITHOSTS_H
