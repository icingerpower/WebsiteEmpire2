#ifndef DIALOGSHOWCOMMAND_H
#define DIALOGSHOWCOMMAND_H

#include <QDialog>
#include <QString>

namespace Ui { class DialogShowCommand; }

/**
 * Resizable dialog that displays a shell command the user can copy and run.
 * Replaces QMessageBox+setDetailedText so the command is immediately visible
 * without an extra click, and the window can be resized to see long commands.
 */
class DialogShowCommand : public QDialog
{
    Q_OBJECT
public:
    explicit DialogShowCommand(const QString &title,
                               const QString &description,
                               const QString &command,
                               QWidget       *parent = nullptr);
    ~DialogShowCommand();

private:
    Ui::DialogShowCommand *ui;
};

#endif // DIALOGSHOWCOMMAND_H
