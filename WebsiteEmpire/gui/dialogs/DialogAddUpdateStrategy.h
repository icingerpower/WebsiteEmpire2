#ifndef DIALOGADDUPDATESTRATEGY_H
#define DIALOGADDUPDATESTRATEGY_H

#include <QDialog>
#include <QString>

namespace Ui {
class DialogAddUpdateStrategy;
}

/**
 * Modal dialog for collecting data needed to add a new update strategy.
 *
 * Collects: name (required), page type, theme (hidden when ≤ 1 theme).
 * The OK button is disabled until a non-empty name is entered.
 */
class DialogAddUpdateStrategy : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddUpdateStrategy(QWidget *parent = nullptr);
    ~DialogAddUpdateStrategy();

    QString name()       const;
    QString pageTypeId() const;
    QString themeId()    const; // empty = all themes

private slots:
    void _onNameChanged(const QString &text);

private:
    Ui::DialogAddUpdateStrategy *ui;
};

#endif // DIALOGADDUPDATESTRATEGY_H
