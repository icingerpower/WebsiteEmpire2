#include "ParamsModel.h"

ParamsModel::ParamsModel(AbstractGenerator *gen, QObject *parent)
    : QAbstractTableModel(parent)
    , m_gen(gen)
    , m_params(gen->currentParams())
{
}

int ParamsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_params.size();
}

int ParamsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return 2;
}

QVariant ParamsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {
    case 0: return tr("Parameter");
    case 1: return tr("Value");
    default: return {};
    }
}

QVariant ParamsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_params.size()) {
        return {};
    }
    const AbstractGenerator::Param &p = m_params.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        if (index.column() == 0) {
            return p.name;
        }
        if (index.column() == 1) {
            return p.defaultValue.toString();
        }
        break;
    case Qt::ToolTipRole:
        return p.tooltip;
    case IsFileRole:
        return p.isFile;
    case IsFolderRole:
        return p.isFolder;
    default:
        break;
    }
    return {};
}

bool ParamsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || index.column() != 1
        || index.row() >= m_params.size()) {
        return false;
    }
    AbstractGenerator::Param &p = m_params[index.row()];
    p.defaultValue = value;
    m_gen->saveParamValue(p.id, value);
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    emit paramChanged();
    return true;
}

Qt::ItemFlags ParamsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 1) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}
