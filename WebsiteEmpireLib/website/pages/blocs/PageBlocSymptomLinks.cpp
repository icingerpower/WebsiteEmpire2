#include "PageBlocSymptomLinks.h"

#include "website/AbstractEngine.h"
#include "website/pages/blocs/widgets/PageBlocSymptomLinksWidget.h"
#include "website/taxonomy/TaxonomyDb.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>

// =============================================================================
// SymptomNav::slugify
// =============================================================================

namespace SymptomNav {

QString slugify(const QString &name)
{
    static const QRegularExpression s_nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression s_combining(QStringLiteral("[\\x{0300}-\\x{036F}]"));
    QString slug = name.toLower().normalized(QString::NormalizationForm_D);
    slug.remove(s_combining);
    slug.replace(s_nonAlnum, QStringLiteral("-"));
    while (slug.startsWith(QLatin1Char('-'))) { slug.remove(0, 1); }
    while (slug.endsWith(QLatin1Char('-')))   { slug.chop(1); }
    return slug;
}

} // namespace SymptomNav

// =============================================================================
// setWorkingDir
// =============================================================================

void PageBlocSymptomLinks::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
}

// =============================================================================
// getName
// =============================================================================

QString PageBlocSymptomLinks::getName() const
{
    return QCoreApplication::translate("PageBlocSymptomLinks", "Symptom Links");
}

// =============================================================================
// load / save
// =============================================================================

void PageBlocSymptomLinks::load(const QHash<QString, QString> &values)
{
    const QStringList parts = values.value(QLatin1String(KEY_SYMPTOMS))
                              .split(QLatin1Char(','), Qt::SkipEmptyParts);
    m_selectedSymptoms.clear();
    for (const QString &p : parts) {
        const QString &name = p.trimmed();
        if (!name.isEmpty()) {
            m_selectedSymptoms.append(name);
        }
    }
}

void PageBlocSymptomLinks::save(QHash<QString, QString> &values) const
{
    values.insert(QLatin1String(KEY_SYMPTOMS), m_selectedSymptoms.join(QLatin1Char(',')));
}

// =============================================================================
// addCode
// =============================================================================

void PageBlocSymptomLinks::addCode(QStringView,
                                    AbstractEngine &engine,
                                    int             websiteIndex,
                                    QString        &html,
                                    QString        &css,
                                    QString        &,
                                    QSet<QString>  &cssDoneIds,
                                    QSet<QString>  &) const
{
    if (m_selectedSymptoms.isEmpty()) {
        return;
    }

    static const QString CSS_ID = QStringLiteral("symptom-links-bloc");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".symptom-links{margin:1em 0}"
            ".symptom-links-label{font-weight:600;margin-right:.4em}"
            ".symptom-links a,.symptom-links span.sym-tag{"
            "display:inline-block;margin:.2em .25em .2em 0;"
            "padding:.2em .65em;border-radius:3em;"
            "background:#e8f0fe;color:#1a73e8;"
            "font-size:.875em;text-decoration:none;white-space:nowrap}"
            ".symptom-links a:hover{background:#c6d9fd}");
    }

    html += QStringLiteral("<div class=\"symptom-links\">");
    html += QStringLiteral("<span class=\"symptom-links-label\">");
    html += QCoreApplication::translate("PageBlocSymptomLinks", "Related symptoms:");
    html += QStringLiteral("</span>");

    for (const QString &name : std::as_const(m_selectedSymptoms)) {
        const QString slug      = SymptomNav::slugify(name);
        const QString permalink = QStringLiteral("/symptoms/") + slug;
        const QString resolved  = engine.resolvePermalink(permalink, websiteIndex);

        if (!resolved.isEmpty() && engine.isPageAvailable(permalink, websiteIndex)) {
            html += QStringLiteral("<a href=\"");
            html += resolved.startsWith(QLatin1Char('/')) ? resolved.mid(1) : resolved;
            html += QStringLiteral("\">");
            html += name;
            html += QStringLiteral("</a>");
        } else {
            html += QStringLiteral("<span class=\"sym-tag\">");
            html += name;
            html += QStringLiteral("</span>");
        }
    }

    html += QStringLiteral("</div>");
}

// =============================================================================
// createEditWidget
// =============================================================================

AbstractPageBlockWidget *PageBlocSymptomLinks::createEditWidget()
{
    return new PageBlocSymptomLinksWidget(loadTaxonomy(m_workingDir));
}

// =============================================================================
// taxonomy / syncTaxonomy / loadTaxonomy
// =============================================================================

std::optional<TaxonomyDescriptor> PageBlocSymptomLinks::taxonomy() const
{
    return TaxonomyDescriptor{
        QStringLiteral("symptoms"),
        QCoreApplication::translate("PageBlocSymptomLinks", "Symptoms")
    };
}

void PageBlocSymptomLinks::syncTaxonomy(const QString &sourceDbPath,
                                         const QDir    &workingDir) const
{
    QStringList names;
    {
        static int s_seed = 0;
        const QString connName = QStringLiteral("symsync_") + QString::number(++s_seed);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(sourceDbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral(
                    "SELECT health_symptom_name FROM records"
                    " ORDER BY health_symptom_name COLLATE NOCASE"));
                while (q.next()) {
                    const QString name = q.value(0).toString().trimmed();
                    if (!name.isEmpty()) {
                        names.append(name);
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connName);
    }
    TaxonomyDb(workingDir).sync(QStringLiteral("symptoms"), names);
}

QStringList PageBlocSymptomLinks::loadTaxonomy(const QDir &workingDir) const
{
    return TaxonomyDb(workingDir).load(QStringLiteral("symptoms"));
}

// =============================================================================
// getAiKeyClues / getAiUpdateSpec
// =============================================================================

QHash<QString, QString> PageBlocSymptomLinks::getAiKeyClues() const
{
    const QStringList vocab = loadTaxonomy(m_workingDir);
    if (vocab.isEmpty()) {
        return {};
    }
    return {{QLatin1String(KEY_SYMPTOMS),
             QCoreApplication::translate("PageBlocSymptomLinks",
                 "Comma-separated symptom names. Choose 0-5 symptoms that the article "
                 "directly addresses. Use ONLY exact names from this list: %1")
                 .arg(vocab.join(QStringLiteral(", ")))}};
}

std::optional<AbstractPageBloc::AiUpdateSpec> PageBlocSymptomLinks::getAiUpdateSpec() const
{
    AiUpdateSpec spec;
    spec.dataKey     = QLatin1String(KEY_SYMPTOMS);
    spec.displayName = QCoreApplication::translate("PageBlocSymptomLinks", "Symptom Links");
    spec.formatPrompt = QCoreApplication::translate("PageBlocSymptomLinks",
        "Based on the article content above, output ONLY a comma-separated list of "
        "symptom names (e.g. \"Knee Pain,Back Pain\"). Choose symptoms the article "
        "directly discusses. Use exact names from the vocabulary — no variations. "
        "Output an empty string if no symptoms apply. No explanation, no other text.");
    spec.validator   = AiUpdateSpec::Validator::NonEmpty;
    return spec;
}
