#ifndef TRANSLATIONSCHEDULER_H
#define TRANSLATIONSCHEDULER_H

#include "website/pages/PageTranslator.h"
#include "website/translation/TranslationSettings.h"

#include <QList>
#include <QString>

class CategoryTable;
class IPageRepository;

/**
 * Builds an ordered list of translation jobs for a single --translate run.
 *
 * Ordering (highest priority first):
 *   Tier 1 — page types listed in TranslationSettings::priorityPageTypes;
 *             within this tier legal pages (PageTypeLegal::TYPE_ID) are first.
 *   Tier 2 — all remaining page types, ordered by page id ASC.
 *
 * Already-complete (page × lang) pairs are excluded: the scheduler loads
 * each page's data and calls isTranslationComplete() so no redundant API
 * requests are dispatched.
 *
 * Pages whose langCodesToTranslate is empty (un-assessed) are skipped —
 * run PageAssessor::assess() first to populate that field.
 *
 * If TranslationSettings::limitPerRun > 0 the returned list is capped at
 * that count after ordering.
 */
class TranslationScheduler
{
public:
    /**
     * Returns ordered, de-duplicated, completeness-filtered jobs ready to
     * hand to PageTranslator::startWithJobs().
     */
    static QList<PageTranslator::TranslationJob>
    buildJobs(IPageRepository           &repo,
              CategoryTable             &categoryTable,
              const TranslationSettings &settings,
              const QString             &editingLang);
};

#endif // TRANSLATIONSCHEDULER_H
