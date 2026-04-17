#ifndef PAGEASSESSOR_H
#define PAGEASSESSOR_H

#include <QStringList>

class IPageRepository;

/**
 * Assigns target languages to source pages that have not yet been assessed.
 *
 * A page is "un-assessed" when its langCodesToTranslate list is empty (the
 * default for newly created pages).  PageAssessor writes the supplied
 * targetLangs to every such source page in a single pass.
 *
 * Idempotent: pages whose langCodesToTranslate is already non-empty are
 * skipped unconditionally — existing assessment results are never overwritten.
 * Translation pages (sourcePageId != 0) are also skipped.
 *
 * Typical usage: run once before TranslationScheduler::buildJobs() so that
 * newly created pages automatically enter the translation queue.
 */
class PageAssessor
{
public:
    /**
     * Scans all pages in repo and writes targetLangs to every un-assessed
     * source page.  Returns the count of pages updated.
     */
    static int assess(IPageRepository   &repo,
                      const QStringList &targetLangs);
};

#endif // PAGEASSESSOR_H
