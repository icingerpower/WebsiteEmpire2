#include "ParamDelegate.h"

#include "aspire/generator/ParamsModel.h"

ParamDelegate::ParamDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *ParamDelegate::createEditor(QWidget *parent,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    // File/folder params are handled externally via WidgetGenerator's doubleClicked
    // handler.  Returning nullptr suppresses the inline editor for those rows so
    // that the QFileDialog is the only editing mechanism.
    if (index.data(ParamsModel::IsFileRole).toBool()
        || index.data(ParamsModel::IsFolderRole).toBool()) {
        return nullptr;
    }
    return QStyledItemDelegate::createEditor(parent, option, index);
}
