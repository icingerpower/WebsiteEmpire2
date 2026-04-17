#include "TranslationStatusTable.h"

#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/attributes/CategoryTable.h"

#include <QCoreApplication>

TranslationStatusTable::TranslationStatusTable(IPageRepository   &repo,
                                               CategoryTable     &categoryTable,
                                               const QStringList &targetLangs,
                                               QObject           *parent)
    : QAbstractTableModel(parent)
    , m_repo(repo)
    , m_categoryTable(categoryTable)
    , m_targetLangs(targetLangs)
{
}

void TranslationStatusTable::reload()
{
    beginResetModel();
    m_rows.clear();

    const QList<PageRecord> &pages = m_repo.findAll();
    for (const PageRecord &page : std::as_const(pages)) {
        if (page.sourcePageId != 0) {
            continue; // only show source pages
        }

        Row row;
        row.record = page;

        auto type = AbstractPageType::createForTypeId(page.typeId, m_categoryTable);
        if (type) {
            const QHash<QString, QString> &data = m_repo.loadData(page.id);
            type->load(data);
            type->setAuthorLang(page.lang);
        }

        for (const QString &lang : std::as_const(m_targetLangs)) {
            const bool complete = type
                ? type->isTranslationComplete(QStringView{}, lang)
                : false;
            row.isComplete.append(complete);
        }

        m_rows.append(row);
    }

    endResetModel();
}

QPair<int, int> TranslationStatusTable::progress() const
{
    int done  = 0;
    int total = 0;
    for (const Row &row : std::as_const(m_rows)) {
        for (bool complete : std::as_const(row.isComplete)) {
            ++total;
            if (complete) {
                ++done;
            }
        }
    }
    return {done, total};
}

int TranslationStatusTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int TranslationStatusTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return COL_COUNT_FIXED + m_targetLangs.size();
}

QVariant TranslationStatusTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }

    const Row &row = m_rows.at(index.row());
    const int  col = index.column();

    if (col < COL_COUNT_FIXED) {
        if (role != Qt::DisplayRole) {
            return {};
        }
        switch (col) {
        case COL_PERMALINK: return row.record.permalink;
        case COL_TYPE:      return row.record.typeId;
        case COL_LANG:      return row.record.lang;
        case COL_PROGRESS: {
            const int total = row.isComplete.size();
            int done = 0;
            for (bool complete : std::as_const(row.isComplete)) {
                if (complete) { ++done; }
            }
            return QString::number(done) + QLatin1Char('/') + QString::number(total);
        }
        default: return {};
        }
    }

    // Dynamic language column
    const int langIdx = col - COL_COUNT_FIXED;
    if (langIdx >= row.isComplete.size()) {
        return {};
    }

    if (role == Qt::CheckStateRole) {
        return row.isComplete.at(langIdx) ? Qt::Checked : Qt::Unchecked;
    }

    return {};
}

QVariant TranslationStatusTable::headerData(int section, Qt::Orientation orientation,
                                            int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case COL_PERMALINK:
        return QCoreApplication::translate("TranslationStatusTable", "Permalink");
    case COL_TYPE:
        return QCoreApplication::translate("TranslationStatusTable", "Type");
    case COL_LANG:
        return QCoreApplication::translate("TranslationStatusTable", "Language");
    case COL_PROGRESS:
        return QCoreApplication::translate("TranslationStatusTable", "Progress");
    default:
        break;
    }

    const int langIdx = section - COL_COUNT_FIXED;
    if (langIdx >= 0 && langIdx < m_targetLangs.size()) {
        return m_targetLangs.at(langIdx);
    }

    return {};
}

Qt::ItemFlags TranslationStatusTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    // Qt draws the checkbox from Qt::CheckStateRole data regardless of whether
    // Qt::ItemIsUserCheckable is present.  Omitting it makes the checkbox
    // display-only — clicks produce no toggle.
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}
