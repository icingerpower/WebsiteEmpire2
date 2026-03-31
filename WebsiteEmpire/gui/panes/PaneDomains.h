#ifndef PANEDOMAINS_H
#define PANEDOMAINS_H

#include <QWidget>

class AbstractEngine;
class HostTable;

namespace Ui {
class PaneDomains;
}

class PaneDomains : public QWidget
{
    Q_OBJECT

public:
    explicit PaneDomains(QWidget *parent = nullptr);
    ~PaneDomains();

    // Binds the pane to an already-init()'d engine.
    // Hides "Apply per lang" when the engine has only one variation.
    // Installs a combo-box delegate on the Host column.
    void setEngine(AbstractEngine *engine);

    // Provides the HostTable needed by the "Edit hosts" dialog.
    void setHostTable(HostTable *hostTable);

public slots:
    void apply();
    void applyPerLang();
    void editHosts();
    void upload();
    void download();
    void viewCommands();

private:
    void _connectSlots();
    QString _applyTemplate(const QString &tmpl,
                           const QString &lang,
                           const QString &theme) const;

    Ui::PaneDomains  *ui;
    AbstractEngine   *m_engine    = nullptr;
    HostTable        *m_hostTable = nullptr;
};

#endif // PANEDOMAINS_H
