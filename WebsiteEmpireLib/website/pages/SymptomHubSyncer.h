#ifndef SYMPTOMHUBSYNCER_H
#define SYMPTOMHUBSYNCER_H

#include <QDir>
#include <QString>

class IPageRepository;

/**
 * Creates stub pages for the symptom navigation system automatically,
 * mirroring the CategoryHubSyncer pattern.
 *
 * syncStubs() opens PageAttributesHealthSymptom.db from the working directory
 * and creates one stub PageRecord per symptom (type_id "symptom_hub",
 * permalink "/symptoms/<slug>") plus a single index page (type_id
 * "symptom_index", permalink "/symptoms").  Stubs whose permalink already
 * exists in pages.db are skipped, so the call is always idempotent.
 *
 * Stubs are created with empty page_data so the AI generation launcher picks
 * them up via findPendingByTypeId() and fills in the social-media metadata
 * (intro paragraph, SEO title, etc.).  The condition list and symptom grid
 * are rendered at generation time from the aspire DB — no stub data needed.
 *
 * No-op (returns 0) when PageAttributesHealthSymptom.db does not exist.
 *
 * Call sites: PaneGeneratedPages::syncStubs(), PaneDomains::deployLocally(),
 * LauncherPublish.
 */
class SymptomHubSyncer
{
public:
    explicit SymptomHubSyncer(IPageRepository &repo);

    /**
     * Creates stub hub pages for every symptom not yet in pages.db,
     * and the /symptoms index page if absent.
     * Returns the total number of stubs created (0 when up to date).
     */
    int syncStubs(const QDir &workingDir, const QString &lang);

private:
    IPageRepository &m_repo;
};

#endif // SYMPTOMHUBSYNCER_H
