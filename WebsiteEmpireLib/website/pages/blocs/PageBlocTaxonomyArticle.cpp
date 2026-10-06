#include "PageBlocTaxonomyArticle.h"
#include "website/AbstractEngine.h"
#include "website/taxonomy/TaxonomyPageSettings.h"

void PageBlocTaxonomyArticle::bindContext(const QDir &workingDir, const QString &taxonomyId)
{
    m_workingDir = workingDir;
    m_taxonomyId = taxonomyId;
}

void PageBlocTaxonomyArticle::addCode(QStringView content, AbstractEngine &engine, int websiteIndex,
                                    QString &html, QString &css, QString &js,
                                    QSet<QString> &cssDoneIds, QSet<QString> &jsDoneIds) const
{
    PageBlocText::addCode(content, engine, websiteIndex, html, css, js, cssDoneIds, jsDoneIds);
    if (text().isEmpty() || m_taxonomyId.isEmpty()) {
        return;
    }
    const QString closing = TaxonomyPageSettings(m_workingDir).translatedClosingText(
        m_taxonomyId, engine.getLangCode(websiteIndex));
    if (!closing.isEmpty()) {
        html += QStringLiteral("<p class=\"taxonomy-closing\">");
        html += closing.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        html += QStringLiteral("</p>");
    }
}

