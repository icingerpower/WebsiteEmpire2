#ifndef PAGEBLOCSYMPTOMLINKS_H
#define PAGEBLOCSYMPTOMLINKS_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QDir>
#include <QStringList>

/**
 * Shared slug utility for the symptom navigation system.
 * Defined in PageBlocSymptomLinks.cpp; declared here so all translation units
 * that include this header can call it without reopening the .cpp namespace.
 */
namespace SymptomNav {
QString slugify(const QString &name);
} // namespace SymptomNav

/**
 * Page bloc for health-condition article pages.
 *
 * Stores the list of symptoms selected by the editor (via
 * PageBlocSymptomLinksWidget) under KEY_SYMPTOMS in page_data.  At render
 * time addCode() reads those stored names and emits pill-style links to the
 * corresponding symptom hub pages at /symptoms/<slug>.
 *
 * KEY_SYMPTOMS is a comma-separated string of canonical symptom names exactly
 * as they appear in the local taxonomy store (taxonomy/taxonomy.db).
 *
 * Call setWorkingDir() from PageTypeArticle::bindGenerationContext() so that
 * createEditWidget() can load the symptom list from the local taxonomy store.
 *
 * Taxonomy integration
 * --------------------
 * taxonomy()       — returns TaxonomyDescriptor{"symptoms", "Symptoms"}
 * syncTaxonomy()   — reads health_symptom_name from the aspire DB and writes
 *                    them into TaxonomyDb under the "symptoms" type.
 * loadTaxonomy()   — reads from TaxonomyDb and returns the ordered list.
 * createEditWidget() — calls loadTaxonomy(m_workingDir) to populate the widget.
 */
class PageBlocSymptomLinks : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_SYMPTOMS = "symptoms";

    ~PageBlocSymptomLinks() override = default;

    /**
     * Stores the working directory so createEditWidget() can load the
     * symptom list from the local taxonomy store.
     * Call from PageTypeArticle::bindGenerationContext().
     */
    void setWorkingDir(const QDir &workingDir);

    QString getName() const override;

    /** Reads KEY_SYMPTOMS and parses it into the internal symptom list. */
    void load(const QHash<QString, QString> &values) override;

    /** Writes the internal symptom list back under KEY_SYMPTOMS. */
    void save(QHash<QString, QString> &values) const override;

    /**
     * Renders pill-style links for each stored symptom.
     * Links are emitted only when engine.isPageAvailable() confirms the hub
     * page exists; otherwise the name is emitted as plain text.
     * No-op when no symptoms are stored.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    AbstractPageBlockWidget *createEditWidget() override;

    /**
     * Returns the vocabulary hint used by the AI generation prompt.
     * Reads from the local taxonomy store (taxonomy/taxonomy.db) in m_workingDir.
     * Returns an empty map when no taxonomy is synced yet — AI will skip this field.
     */
    QHash<QString, QString> getAiKeyClues() const override;

    /**
     * Tells the AI generation pipeline to fill KEY_SYMPTOMS with a
     * comma-separated list of symptom names chosen from the vocabulary.
     */
    std::optional<AiUpdateSpec> getAiUpdateSpec() const override;

    std::optional<TaxonomyDescriptor> taxonomy() const override;
    void syncTaxonomy(const QString &sourceDbPath, const QDir &workingDir) const override;
    QStringList loadTaxonomy(const QDir &workingDir) const override;

private:
    QDir        m_workingDir;
    QStringList m_selectedSymptoms;
};

#endif // PAGEBLOCSYMPTOMLINKS_H
