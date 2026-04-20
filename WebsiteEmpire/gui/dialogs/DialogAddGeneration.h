#ifndef DIALOGADDGENERATION_H
#define DIALOGADDGENERATION_H

#include <QDialog>
#include <QString>

class AbstractEngine;

namespace Ui {
class DialogAddGeneration;
}

// Modal dialog for collecting the data needed to add a new generation strategy.
//
// The OK button is disabled until a non-empty name is entered.
// When custom instructions are provided, they must contain [TOPIC]; otherwise
// clicking OK shows a QMessageBox::warning and the dialog stays open.
// The Theme row (label + combo) is hidden when only one theme is registered,
// since there is no meaningful choice to make.  In that case themeId() returns
// an empty string (meaning "all themes / no theme filter").
//
// engine (nullable) is used to pre-select the source table combo:
// if the engine declares a generator via getGeneratorId(), the matching primary
// table is selected by default.  The user can always change the selection.
//
// After accept(), read the result via name(), pageTypeId(), themeId(),
// nonSvgImages(), primaryAttrId(), and customInstructions().
class DialogAddGeneration : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddGeneration(AbstractEngine *engine, QWidget *parent = nullptr);
    ~DialogAddGeneration();

    QString name()               const;
    QString pageTypeId()         const;
    QString themeId()            const; // empty = all themes
    QString primaryAttrId()      const; // empty = no source table linked
    QString customInstructions() const; // empty = use generic prompt
    bool    nonSvgImages()       const;

private slots:
    void _onNameChanged(const QString &text);
    void _onAccepted();

private:
    Ui::DialogAddGeneration *ui;
};

#endif // DIALOGADDGENERATION_H
