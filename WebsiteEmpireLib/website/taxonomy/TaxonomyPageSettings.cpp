#include "TaxonomyPageSettings.h"
#include "website/pages/HubSeoTemplateDb.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/blocs/PageBlocFashionTaxonomyLinks.h"
#include "website/pages/blocs/PageBlocSymptomLinks.h"
#include "TaxonomyDb.h"
#include "ExceptionWithTitleText.h"
#include <QCryptographicHash>
#include <QSettings>
#include <QSet>

TaxonomyPageSettings::TaxonomyPageSettings(const QDir &workingDir) : m_workingDir(workingDir)
{
}

QString TaxonomyPageSettings::strategyId(const QString &taxonomyId) const
{
    QSettings settings(m_workingDir.filePath(QStringLiteral("taxonomy_pages.ini")), QSettings::IniFormat);
    return settings.value(taxonomyId + QStringLiteral("/strategy")).toString();
}

void TaxonomyPageSettings::setStrategyId(const QString &taxonomyId, const QString &id)
{
    QSettings settings(m_workingDir.filePath(QStringLiteral("taxonomy_pages.ini")), QSettings::IniFormat);
    settings.setValue(taxonomyId + QStringLiteral("/strategy"), id);
}

QString TaxonomyPageSettings::closingText(const QString &taxonomyId) const
{
    QSettings settings(m_workingDir.filePath(QStringLiteral("taxonomy_pages.ini")), QSettings::IniFormat);
    return settings.value(taxonomyId + QStringLiteral("/closingText")).toString();
}

void TaxonomyPageSettings::setClosingText(const QString &taxonomyId, const QString &text)
{
    QSettings settings(m_workingDir.filePath(QStringLiteral("taxonomy_pages.ini")), QSettings::IniFormat);
    settings.setValue(taxonomyId + QStringLiteral("/closingText"), text);
}

QString TaxonomyPageSettings::translationKey(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QMap<QString, QMap<QString, QString>> TaxonomyPageSettings::translationTemplates() const
{
    QMap<QString, QMap<QString, QString>> result;
    QSettings settings(m_workingDir.filePath(QStringLiteral("taxonomy_pages.ini")), QSettings::IniFormat);
    const auto groups = settings.childGroups();
    for (const QString &id : groups) {
        const QString text = closingText(id);
        if (!text.isEmpty()) {
            result.insert(QStringLiteral("taxonomy_closing_") + id, {{translationKey(text), text}});
        }
    }
    return result;
}

QString TaxonomyPageSettings::translatedClosingText(const QString &taxonomyId, const QString &lang) const
{
    const QString source = closingText(taxonomyId);
    if (source.isEmpty()) {
        return {};
    }
    const QString translated = HubSeoTemplateDb(m_workingDir).get(
        QStringLiteral("taxonomy_closing_") + taxonomyId, translationKey(source), lang);
    return translated.isEmpty() ? source : translated;
}

QList<PageRecord> TaxonomyPageSettings::pendingPages(const QString &taxonomyId, const QString &lang,
                                                   IPageRepository &repo) const
{
    QString prefix;
    const bool symptoms = taxonomyId == QStringLiteral("symptoms");
    if (symptoms) {
        prefix = QStringLiteral("/symptoms/");
    } else {
        for (const auto &dim : PageBlocFashionTaxonomyLinks::dimensions()) {
            if (dim.taxonomyId == taxonomyId) {
                prefix = dim.hubPrefix;
                break;
            }
        }
    }
    if (prefix.isEmpty()) {
        ExceptionWithTitleText ex(QObject::tr("Unknown taxonomy"),
            QObject::tr("Article generation is not available for taxonomy %1.").arg(taxonomyId));
        ex.raise();
    }
    QList<PageRecord> result;
    const QStringList names = TaxonomyDb(m_workingDir).load(taxonomyId);
    const QList<PageRecord> existing = repo.findAll();
    QHash<QString, PageRecord> hubs;
    QSet<QString> occupiedPermalinks;
    QSet<int> queuedIds;
    const QString typeId = symptoms ? QStringLiteral("symptom_hub") : QStringLiteral("fashion_tag_hub");
    for (const PageRecord &candidate : existing) {
        occupiedPermalinks.insert(candidate.permalink);
        if (candidate.sourcePageId != 0 || candidate.typeId != typeId) {
            continue;
        }
        if (symptoms) {
            hubs.insert(candidate.permalink, candidate);
        } else {
            const auto data = repo.loadData(candidate.id);
            if (data.value(QStringLiteral("0_dimension")) == taxonomyId) {
                hubs.insert(data.value(QStringLiteral("0_tag_value")), candidate);
            }
        }
    }
    for (const QString &name : names) {
        const QString slug = symptoms ? SymptomNav::slugify(name) : PageBlocFashionTaxonomyLinks::slugify(name);
        if (slug.isEmpty()) {
            continue;
        }
        const QString permalink = prefix + slug;
        PageRecord page;
        page.typeId = typeId;
        page.permalink = permalink;
        page.lang = lang;
        const auto existingHub = hubs.constFind(symptoms ? permalink : name);
        if (existingHub != hubs.cend()) {
            page = existingHub.value();
        }
        if (page.id == 0) {
            if (!symptoms) {
                int suffix = 2;
                while (occupiedPermalinks.contains(page.permalink)) {
                    page.permalink = permalink + QLatin1Char('-') + QString::number(suffix++);
                }
            }
            page.id = repo.create(page.typeId, page.permalink, lang);
            if (page.id <= 0) {
                ExceptionWithTitleText ex(QObject::tr("Taxonomy URL conflict"),
                    QObject::tr("Cannot create the taxonomy page at %1 because that URL is already used.").arg(permalink));
                ex.raise();
            }
            if (!symptoms) {
                repo.saveData(page.id, {{QStringLiteral("0_dimension"), taxonomyId},
                                        {QStringLiteral("0_tag_value"), name}});
            }
            hubs.insert(symptoms ? permalink : name, page);
            occupiedPermalinks.insert(page.permalink);
        }
        const auto data = repo.loadData(page.id);
        if (data.value(QStringLiteral("_taxonomyArticle")) != QStringLiteral("1")
            && !queuedIds.contains(page.id)) {
            // Automatic HTML publication can mark a stub Complete. This queue
            // explicitly upgrades it, without altering its current published data.
            page.generationState = PageGenerationState::Pending;
            result.append(page);
            queuedIds.insert(page.id);
        }
    }
    return result;
}
