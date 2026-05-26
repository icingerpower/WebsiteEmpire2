#include "PageBlocMeta.h"
#include "website/pages/blocs/widgets/PageBlocMetaWidget.h"

#include <QCoreApplication>

// =============================================================================
// getName / getAiKeyClues
// =============================================================================

QString PageBlocMeta::getName() const
{
    return QCoreApplication::translate("PageBlocMeta", "SEO Meta");
}

QHash<QString, QString> PageBlocMeta::getAiKeyClues() const
{
    return {
        {QLatin1String(KEY_SEO_TITLE),
         QCoreApplication::translate("PageBlocMeta",
             "SEO page title, 50–60 chars, must include the main keyword")},
        {QLatin1String(KEY_SEO_DESC),
         QCoreApplication::translate("PageBlocMeta",
             "Meta description, 120–160 chars, summarises the page content for search snippets")},
    };
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocMeta::load(const QHash<QString, QString> &values)
{
    m_title       = values.value(QLatin1String(KEY_SEO_TITLE));
    m_description = values.value(QLatin1String(KEY_SEO_DESC));
    m_translations.setSource(QLatin1String(KEY_SEO_TITLE), m_title);
    m_translations.setSource(QLatin1String(KEY_SEO_DESC),  m_description);
    m_translations.loadFromMap(values);
}

void PageBlocMeta::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_SEO_TITLE), m_title);
    values.insert(QLatin1String(KEY_SEO_DESC),  m_description);
    m_translations.saveToMap(values);
}

// =============================================================================
// addCode — no-op (all output goes into <head> via buildHeadMetaTags)
// =============================================================================

void PageBlocMeta::addCode(QStringView, AbstractEngine &, int,
                            QString &, QString &, QString &,
                            QSet<QString> &, QSet<QString> &) const
{
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocMeta::createEditWidget()
{
    return new PageBlocMetaWidget();
}

// =============================================================================
// Translation protocol
// =============================================================================

void PageBlocMeta::collectTranslatables(QStringView, QList<TranslatableField> &out) const
{
    if (!m_title.isEmpty()) {
        TranslatableField f;
        f.id         = QLatin1String(KEY_SEO_TITLE);
        f.sourceText = m_title;
        out.append(f);
    }
    if (!m_description.isEmpty()) {
        TranslatableField f;
        f.id         = QLatin1String(KEY_SEO_DESC);
        f.sourceText = m_description;
        out.append(f);
    }
}

void PageBlocMeta::applyTranslation(QStringView, const QString &fieldId,
                                     const QString &lang, const QString &text)
{
    m_translations.setTranslation(fieldId, lang, text);
}

bool PageBlocMeta::isTranslationComplete(QStringView, const QString &lang) const
{
    return m_translations.isComplete(lang);
}

// =============================================================================
// Accessors
// =============================================================================

QString PageBlocMeta::seoTitle(const QString &lang) const
{
    const QString &tr = m_translations.translation(QLatin1String(KEY_SEO_TITLE), lang);
    return tr.isEmpty() ? m_title : tr;
}

QString PageBlocMeta::seoDescription(const QString &lang) const
{
    const QString &tr = m_translations.translation(QLatin1String(KEY_SEO_DESC), lang);
    return tr.isEmpty() ? m_description : tr;
}
