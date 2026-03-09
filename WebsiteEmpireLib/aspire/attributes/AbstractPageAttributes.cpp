#include "AbstractPageAttributes.h"

const QStringList AbstractPageAttributes::COLUMNS{
    QObject::tr("Name")
    , QObject::tr("Description")
    , QObject::tr("Default")
    , QObject::tr("Exemple")
};

AbstractPageAttributes::AbstractPageAttributes(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void AbstractPageAttributes::init()
{
    const auto &attributes = getAttributes();
    for (const auto &attribute : *attributes) {
        m_listOfStringList << QStringList{};
        m_listOfStringList.last() << attribute.name;
        m_listOfStringList.last() << attribute.description;
        m_listOfStringList.last() << attribute.valueDefault;
        m_listOfStringList.last() << attribute.valueExemple;
    }
}

AbstractPageAttributes::Recorder::Recorder(AbstractPageAttributes *pageAttributes)
{
    pageAttributes->init();
    getPageAttributes()[pageAttributes->getId()] = pageAttributes;
}

const QMap<QString, const AbstractPageAttributes *> &AbstractPageAttributes::ALL_PAGE_ATTRIBUTES()
{
    return getPageAttributes();
}

QMap<QString, const AbstractPageAttributes *> &AbstractPageAttributes::getPageAttributes()
{
    static QMap<QString, const AbstractPageAttributes *> pageAttributes;
    return pageAttributes;
}

int AbstractPageAttributes::rowCount(const QModelIndex &) const
{
    return m_listOfStringList.size();
}

int AbstractPageAttributes::columnCount(const QModelIndex &) const
{
    return COLUMNS.size();
}

QVariant AbstractPageAttributes::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole) {
        return m_listOfStringList[index.row()][index.column()];
    }
    return QVariant();
}
