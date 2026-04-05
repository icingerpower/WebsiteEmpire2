#include "AbstractPageType.h"
#include "website/AbstractEngine.h"
#include "website/pages/blocs/AbstractPageBloc.h"

#include <QHash>
#include <QString>

// =============================================================================
// Registry internals
// =============================================================================

namespace {

struct PageTypeEntry {
    QString                        displayName;
    AbstractPageType::Factory      factory;
};

QHash<QString, PageTypeEntry> &registry()
{
    static QHash<QString, PageTypeEntry> s_registry;
    return s_registry;
}

// Insertion-order list of type ids (QHash does not preserve order).
QList<QString> &registryOrder()
{
    static QList<QString> s_order;
    return s_order;
}

} // namespace

// =============================================================================
// Recorder
// =============================================================================

AbstractPageType::Recorder::Recorder(const QString &typeId,
                                      const QString &displayName,
                                      Factory        factory)
{
    Q_ASSERT_X(!registry().contains(typeId),
               "AbstractPageType::Recorder",
               qPrintable(QStringLiteral("Duplicate page type id: ") + typeId));
    registry().insert(typeId, {displayName, std::move(factory)});
    registryOrder().append(typeId);
}

// =============================================================================
// Static factory / query
// =============================================================================

std::unique_ptr<AbstractPageType> AbstractPageType::createForTypeId(const QString &typeId,
                                                                      CategoryTable &table)
{
    const auto it = registry().find(typeId);
    if (it == registry().end()) {
        return nullptr;
    }
    return it->factory(table);
}

QList<QString> AbstractPageType::allTypeIds()
{
    return registryOrder();
}

// =============================================================================
// load / save
// =============================================================================

void AbstractPageType::load(const QHash<QString, QString> &values)
{
    const auto &blocs = getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        const QString prefix = QString::number(i) + QStringLiteral("_");
        QHash<QString, QString> sub;
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (it.key().startsWith(prefix)) {
                sub.insert(it.key().mid(prefix.size()), it.value());
            }
        }
        // load() mutates bloc state: const_cast is safe because AbstractPageType
        // owns the blocs and load() is the designated mutation path.
        const_cast<AbstractPageBloc *>(blocs.at(i))->load(sub);
    }
}

void AbstractPageType::save(QHash<QString, QString> &values) const
{
    const auto &blocs = getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        const QString prefix = QString::number(i) + QStringLiteral("_");
        QHash<QString, QString> sub;
        blocs.at(i)->save(sub);
        for (auto it = sub.cbegin(); it != sub.cend(); ++it) {
            values.insert(prefix + it.key(), it.value());
        }
    }
}

// =============================================================================
// addCode
// =============================================================================

void AbstractPageType::addCode(QStringView     origContent,
                                AbstractEngine &engine,
                                int             websiteIndex,
                                QString        &html,
                                QString        &css,
                                QString        &js,
                                QSet<QString>  &cssDoneIds,
                                QSet<QString>  &jsDoneIds) const
{
    // Accumulate bloc output in temporary buffers so CSS can go into <head>
    // and JS before </body>.
    QString bodyHtml, innerCss, innerJs;
    for (const AbstractPageBloc *bloc : getPageBlocs()) {
        bloc->addCode(origContent, engine, websiteIndex, bodyHtml, innerCss, innerJs, cssDoneIds, jsDoneIds);
    }

    const QString langCode = engine.getLangCode(websiteIndex);
    html += QStringLiteral("<!DOCTYPE html><html");
    if (!langCode.isEmpty()) {
        html += QStringLiteral(" lang=\"");
        html += langCode;
        html += QStringLiteral("\"");
    }
    html += QStringLiteral("><head><meta charset=\"utf-8\">");
    if (!innerCss.isEmpty()) {
        html += QStringLiteral("<style>");
        html += innerCss;
        html += QStringLiteral("</style>");
    }
    html += QStringLiteral("</head><body>");
    html += bodyHtml;
    if (!innerJs.isEmpty()) {
        html += QStringLiteral("<script>");
        html += innerJs;
        html += QStringLiteral("</script>");
    }
    html += QStringLiteral("</body></html>");

    // css and js are intentionally left untouched — all output is inlined
    // into html. A page type is the top-level generator, not a fragment.
    Q_UNUSED(css)
    Q_UNUSED(js)
}

// =============================================================================
// getAttributes
// =============================================================================

const QList<const AbstractAttribute *> &AbstractPageType::getAttributes() const
{
    if (!m_attributesCached) {
        for (const AbstractPageBloc *bloc : getPageBlocs()) {
            const auto &attrs = bloc->getAttributes();
            m_cachedAttributes += attrs;
        }
        m_attributesCached = true;
    }
    return m_cachedAttributes;
}
