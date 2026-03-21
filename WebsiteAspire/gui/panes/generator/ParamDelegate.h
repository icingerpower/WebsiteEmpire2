#ifndef PARAMDELEGATE_H
#define PARAMDELEGATE_H

#include <QStyledItemDelegate>

// Item delegate for the ParamsModel parameter table.
//
// File and folder params (detected via ParamsModel::IsFileRole /
// ParamsModel::IsFolderRole) return nullptr from createEditor(), suppressing
// the inline QLineEdit so that WidgetGenerator's doubleClicked handler can open
// a QFileDialog instead.  All other params fall through to the default
// QStyledItemDelegate behaviour (inline QLineEdit editor).
class ParamDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ParamDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
};

#endif // PARAMDELEGATE_H
