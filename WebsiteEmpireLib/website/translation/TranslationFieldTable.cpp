#include "TranslationFieldTable.h"

#include "website/commonblocs/AbstractCommonBloc.h"

#include <QCoreApplication>

TranslationFieldTable::TranslationFieldTable(const QList<AbstractCommonBloc *> &blocs,
                                             const QStringList                 &targetLangs,
                                             QObject                           *parent)
    : QAbstractTableModel(parent)
    , m_targetLangs(targetLangs)
{
    for (AbstractCommonBloc *bloc : std::as_const(blocs)) {
        if (!bloc) {
            continue;
        }

        const QHash<QString, QString> &texts = bloc->sourceTexts();
        if (texts.isEmpty()) {
            continue;
        }

        const QString &blocName = bloc->getName();
        for (auto it = texts.cbegin(); it != texts.cend(); ++it) {
            if (it.value().isEmpty()) {
                continue;
            }

            Row row;
            row.bloc       = bloc;
            row.blocName   = blocName;
            row.fieldId    = it.key();
            row.sourceText = it.value();

            for (const QString &lang : std::as_const(targetLangs)) {
                row.translations.append(bloc->translatedText(it.key(), lang));
            }

            m_rows.append(row);
        }
    }
}

int TranslationFieldTable::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

int TranslationFieldTable::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return COL_COUNT_FIXED + m_targetLangs.size();
}

QPair<int, int> TranslationFieldTable::progress() const
{
    int done  = 0;
    int total = 0;
    for (const Row &row : std::as_const(m_rows)) {
        for (const QString &tr : std::as_const(row.translations)) {
            ++total;
            if (!tr.isEmpty()) {
                ++done;
            }
        }
    }
    return {done, total};
}

bool TranslationFieldTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    const int col = index.column();
    if (col < COL_COUNT_FIXED || index.row() >= m_rows.size()) {
        return false;
    }
    const int langIdx = col - COL_COUNT_FIXED;
    if (langIdx >= m_targetLangs.size()) {
        return false;
    }

    Row &row = m_rows[index.row()];
    const QString &text = value.toString();
    row.translations[langIdx] = text;
    row.bloc->setTranslation(row.fieldId, m_targetLangs.at(langIdx), text);
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QVariant TranslationFieldTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return {};
    }

    const Row &row = m_rows.at(index.row());
    const int  col = index.column();

    switch (col) {
    case COL_BLOC_NAME:   return row.blocName;
    case COL_FIELD_LABEL: return row.fieldId;
    case COL_SOURCE:      return row.sourceText;
    case COL_PROGRESS: {
        const int total = row.translations.size();
        int done = 0;
        for (const QString &t : std::as_const(row.translations)) {
            if (!t.isEmpty()) { ++done; }
        }
        return QString::number(done) + QLatin1Char('/') + QString::number(total);
    }
    default: break;
    }

    const int langIdx = col - COL_COUNT_FIXED;
    if (langIdx >= 0 && langIdx < row.translations.size()) {
        return row.translations.at(langIdx);
    }

    return {};
}

QVariant TranslationFieldTable::headerData(int section, Qt::Orientation orientation,
                                           int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case COL_BLOC_NAME:
        return QCoreApplication::translate("TranslationFieldTable", "Bloc");
    case COL_FIELD_LABEL:
        return QCoreApplication::translate("TranslationFieldTable", "Field");
    case COL_SOURCE:
        return QCoreApplication::translate("TranslationFieldTable", "Source");
    case COL_PROGRESS:
        return QCoreApplication::translate("TranslationFieldTable", "Progress");
    default:
        break;
    }

    const int langIdx = section - COL_COUNT_FIXED;
    if (langIdx >= 0 && langIdx < m_targetLangs.size()) {
        return m_targetLangs.at(langIdx);
    }

    return {};
}

Qt::ItemFlags TranslationFieldTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    if (index.column() >= COL_COUNT_FIXED) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}
