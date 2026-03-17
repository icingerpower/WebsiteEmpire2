#ifndef DIALOGVIEWATTRIBUTES_H
#define DIALOGVIEWATTRIBUTES_H

#include <QDialog>

class AbstractPageAttributes;

namespace Ui {
class DialogViewAttributes;
}

// Read-only dialog that displays the attribute schema of a page-attributes
// model (name, description, default value, example) in a QTableView with
// properly labelled column headers.
class DialogViewAttributes : public QDialog
{
    Q_OBJECT

public:
    explicit DialogViewAttributes(const AbstractPageAttributes *pageAttributes,
                                  QWidget *parent = nullptr);
    ~DialogViewAttributes();

private:
    Ui::DialogViewAttributes *ui;
};

#endif // DIALOGVIEWATTRIBUTES_H
