#ifndef DIALOGADDUPDATEPROMPT_H
#define DIALOGADDUPDATEPROMPT_H

#include "website/pages/AbstractPageType.h"
#include <QDialog>
#include <QList>

namespace Ui { class DialogAddUpdatePrompt; }

/**
 * Dialog for adding a new prompt step to an update strategy.
 * Shows a name field and a dropdown of available AI update targets
 * derived from the strategy's page type (via AbstractPageType::aiUpdateTargets()).
 */
class DialogAddUpdatePrompt : public QDialog
{
    Q_OBJECT
public:
    explicit DialogAddUpdatePrompt(const QList<AbstractPageType::AiUpdateTarget> &targets,
                                   QWidget *parent = nullptr);
    ~DialogAddUpdatePrompt();

    QString name()              const;
    QString saveKey()           const;  // empty when article-text target selected
    bool    skipIfKeyNonEmpty() const;

private slots:
    void _onTargetChanged(int index);
    void _validate();

private:
    Ui::DialogAddUpdatePrompt *ui;
    QList<AbstractPageType::AiUpdateTarget> m_targets;
};

#endif // DIALOGADDUPDATEPROMPT_H
