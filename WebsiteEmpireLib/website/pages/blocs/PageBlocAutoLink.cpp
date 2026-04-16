#include "PageBlocAutoLink.h"
#include "website/pages/LinksManager.h"
#include "website/pages/blocs/widgets/PageBlocAutoLinkWidget.h"

#include <QCoreApplication>

// =============================================================================
// Accessors
// =============================================================================

void PageBlocAutoLink::setPageUrl(const QString &url)
{
    m_pageUrl = url;
}

QString PageBlocAutoLink::pageUrl() const
{
    return m_pageUrl;
}

const QStringList &PageBlocAutoLink::keywords() const
{
    return m_keywords;
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocAutoLink::getName() const
{
    return QCoreApplication::translate("PageBlocAutoLink", "Auto-Link Keywords");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocAutoLink::load(const QHash<QString, QString> &values)
{
    m_pageUrl = values.value(QLatin1String(KEY_PAGE_URL));

    const int count = values.value(QLatin1String(KEY_KW_COUNT)).toInt();
    m_keywords.clear();
    m_keywords.reserve(count);
    for (int i = 0; i < count; ++i) {
        const QString &kw = values.value(QStringLiteral("kw_") + QString::number(i));
        if (!kw.isEmpty()) {
            m_keywords.append(kw);
        }
    }
}

void PageBlocAutoLink::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_PAGE_URL), m_pageUrl);
    values.insert(QLatin1String(KEY_KW_COUNT), QString::number(m_keywords.size()));
    for (int i = 0; i < m_keywords.size(); ++i) {
        values.insert(QStringLiteral("kw_") + QString::number(i), m_keywords.at(i));
    }

    // Keep the central link registry up-to-date immediately after each save
    // so that consumers (e.g. the LinksManager table view) reflect current state
    // without requiring a full site re-scan.
    LinksManager::instance().setKeywords(m_pageUrl, m_keywords);
}

// =============================================================================
// addCode
// =============================================================================

void PageBlocAutoLink::addCode(QStringView      /*origContent*/,
                                AbstractEngine  & /*engine*/,
                                int              /*websiteIndex*/,
                                QString         & /*html*/,
                                QString         & /*css*/,
                                QString         & /*js*/,
                                QSet<QString>   & /*cssDoneIds*/,
                                QSet<QString>   & /*jsDoneIds*/) const
{
    // Auto-link substitution is performed by the page generator across all
    // pages; this bloc contributes no direct body HTML output.
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocAutoLink::createEditWidget()
{
    return new PageBlocAutoLinkWidget;
}
