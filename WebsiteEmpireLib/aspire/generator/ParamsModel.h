#ifndef PARAMSMODEL_H
#define PARAMSMODEL_H

#include <QAbstractTableModel>
#include <QList>

#include "aspire/generator/AbstractGenerator.h"

// Two-column (Parameter | Value) model backed by AbstractGenerator::Param list.
//
// Column 0 — Parameter name, read-only.
// Column 1 — Current value, editable.
//
// setData() persists every change immediately via AbstractGenerator::saveParamValue()
// and emits paramChanged() so the owning widget can re-validate and refresh stats.
//
// Custom roles on the Value column:
//   IsFileRole   (Qt::UserRole+1) — bool, true when the param is a file path.
//   IsFolderRole (Qt::UserRole+2) — bool, true when the param is a folder path.
// These are read by ParamDelegate to decide whether to suppress the inline editor.
class ParamsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum CustomRole {
        IsFileRole   = Qt::UserRole + 1,
        IsFolderRole = Qt::UserRole + 2,
    };

    explicit ParamsModel(AbstractGenerator *gen, QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = {})                        const override;
    int      columnCount(const QModelIndex &parent = {})                     const override;
    QVariant headerData(int section, Qt::Orientation, int role)              const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole)      const override;
    bool     setData(const QModelIndex &index, const QVariant &value,
                     int role = Qt::EditRole)                                       override;
    Qt::ItemFlags flags(const QModelIndex &index)                            const override;

signals:
    // Emitted after every successful setData() call (value persisted).
    void paramChanged();

private:
    AbstractGenerator *m_gen;
    QList<AbstractGenerator::Param> m_params; // current values, updated on setData()
};

#endif // PARAMSMODEL_H
