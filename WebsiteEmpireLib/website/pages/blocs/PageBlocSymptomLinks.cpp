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
// addCode helpers
// =============================================================================

static const QString &_relatedSymptomsLabel(const QString &lang)
{
    static const QHash<QString, QString> s_labels = {
        {QStringLiteral("en"), QStringLiteral("Related symptoms:")},
        {QStringLiteral("fr"), QStringLiteral("Symptômes associés :")},
        {QStringLiteral("de"), QStringLiteral("Verwandte Symptome:")},
        {QStringLiteral("ja"), QStringLiteral("関連症状：")},
        {QStringLiteral("es"), QStringLiteral("Síntomas relacionados:")},
        {QStringLiteral("pt"), QStringLiteral("Sintomas relacionados:")},
        {QStringLiteral("it"), QStringLiteral("Sintomi correlati:")},
        {QStringLiteral("nl"), QStringLiteral("Gerelateerde symptomen:")},
        {QStringLiteral("pl"), QStringLiteral("Powiązane objawy:")},
        {QStringLiteral("ru"), QStringLiteral("Связанные симптомы:")},
        {QStringLiteral("zh"), QStringLiteral("相关症状：")},
        {QStringLiteral("ar"), QStringLiteral("الأعراض ذات الصلة:")},
        {QStringLiteral("ko"), QStringLiteral("관련 증상:")},
        {QStringLiteral("hi"), QStringLiteral("संबंधित लक्षण:")},
        {QStringLiteral("tr"), QStringLiteral("İlgili belirtiler:")},
        {QStringLiteral("id"), QStringLiteral("Gejala terkait:")},
        {QStringLiteral("ms"), QStringLiteral("Simptom berkaitan:")},
        {QStringLiteral("vi"), QStringLiteral("Triệu chứng liên quan:")},
        {QStringLiteral("th"), QStringLiteral("อาการที่เกี่ยวข้อง:")},
        {QStringLiteral("uk"), QStringLiteral("Пов'язані симптоми:")},
        {QStringLiteral("sv"), QStringLiteral("Relaterade symptom:")},
        {QStringLiteral("ro"), QStringLiteral("Simptome înrudite:")},
        {QStringLiteral("hu"), QStringLiteral("Kapcsolódó tünetek:")},
        {QStringLiteral("el"), QStringLiteral("Σχετικά συμπτώματα:")},
        {QStringLiteral("fa"), QStringLiteral("علائم مرتبط:")},
        {QStringLiteral("bn"), QStringLiteral("সম্পর্কিত লক্ষণ:")},
        {QStringLiteral("sw"), QStringLiteral("Dalili zinazohusiana:")},
        {QStringLiteral("pa"), QStringLiteral("ਸੰਬੰਧਿਤ ਲੱਛਣ:")},
        {QStringLiteral("ta"), QStringLiteral("தொடர்புடைய அறிகுறிகள்:")},
        {QStringLiteral("te"), QStringLiteral("సంబంధిత లక్షణాలు:")},
        {QStringLiteral("mr"), QStringLiteral("संबंधित लक्षणे:")},
        {QStringLiteral("ur"), QStringLiteral("متعلقہ علامات:")},
    };
    static const QString s_fallback = QStringLiteral("Related symptoms:");
    const auto it = s_labels.constFind(lang);
    return it != s_labels.constEnd() ? it.value() : s_fallback;
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

    struct SymLink { QString name; QString href; };
    QList<SymLink> links;
    for (const QString &name : std::as_const(m_selectedSymptoms)) {
        const QString slug      = SymptomNav::slugify(name);
        const QString permalink = QStringLiteral("/symptoms/") + slug;
        if (!engine.isPageAvailable(permalink, websiteIndex)) {
            continue;
        }
        const QString resolved = engine.resolveLinkHref(permalink, websiteIndex);
        if (resolved.isEmpty()) {
            continue;
        }
        links.append({name, resolved});
    }

    if (links.isEmpty()) {
        return;
    }

    static const QString CSS_ID = QStringLiteral("symptom-links-bloc");
    if (!cssDoneIds.contains(CSS_ID)) {
        cssDoneIds.insert(CSS_ID);
        css += QStringLiteral(
            ".symptom-links{margin:1em 0}"
            ".symptom-links-label{font-weight:600;margin-right:.4em}"
            ".symptom-links a{"
            "display:inline-block;margin:.2em .25em .2em 0;"
            "padding:.2em .65em;border-radius:3em;"
            "background:#e8f0fe;color:#1a73e8;"
            "font-size:.875em;text-decoration:none;white-space:nowrap}"
            ".symptom-links a:hover{background:#c6d9fd}");
    }

    html += QStringLiteral("<div class=\"symptom-links\">");
    html += QStringLiteral("<span class=\"symptom-links-label\">");
    html += _relatedSymptomsLabel(engine.getLangCode(websiteIndex));
    html += QStringLiteral("</span>");

    for (const auto &link : std::as_const(links)) {
        html += QStringLiteral("<a href=\"");
        html += link.href;
        html += QStringLiteral("\">");
        html += link.name;
        html += QStringLiteral("</a>");
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
    spec.dataKey      = QLatin1String(KEY_SYMPTOMS);
    spec.displayName  = QCoreApplication::translate("PageBlocSymptomLinks", "Symptom Links");
    spec.formatPrompt = QCoreApplication::translate("PageBlocSymptomLinks",
        "Based on the article content above, output ONLY a comma-separated list of "
        "symptom names (e.g. \"Knee Pain,Back Pain\"). Choose symptoms the article "
        "directly discusses. Use exact names from the vocabulary — no variations. "
        "Output an empty string if no symptoms apply. No explanation, no other text.");
    spec.validator    = AiUpdateSpec::Validator::CommaSeparatedVocabulary;
    spec.allowedValues = loadTaxonomy(m_workingDir);
    return spec;
}
