#ifndef ABSTRACTREVIEWACTION_H
#define ABSTRACTREVIEWACTION_H

#include <QList>
#include <QString>

class IPageRepository;
struct PageRecord;

/**
 * Base class for review actions executed by LauncherReview (--review).
 *
 * A review action inspects a page's performance data and takes automated
 * decisions — e.g. enabling the social-media second pass once a page reaches
 * a display-count threshold, or marking a low-performing page for rewrite.
 *
 * Self-registration pattern
 * -------------------------
 * Each concrete subclass places DECLARE_REVIEW_ACTION(ClassName) at file scope
 * in its .cpp.  All registered actions are accessible via all().
 *
 * LauncherReview iterates all() for every source page and calls run().
 */
class AbstractReviewAction
{
public:
    virtual ~AbstractReviewAction() = default;

    /** Stable identifier for logging and diagnostics. */
    virtual QString getId() const = 0;

    /** Human-readable name shown in --review output. */
    virtual QString getDisplayName() const = 0;

    /**
     * Runs the review action for a single source page.
     *
     * displayCount is the number of impressions the page's URL has received
     * over the review window (from the active performance data source).
     *
     * Implementations may call repo.setFlag() or other mutating methods.
     * Returns true when the action made a change that was persisted.
     */
    virtual bool run(const PageRecord &page,
                     int               displayCount,
                     IPageRepository  &repo) = 0;

    /** Returns all registered review actions in registration order. */
    static QList<AbstractReviewAction *> all();

    /**
     * Place at file scope in each concrete subclass's .cpp via
     * DECLARE_REVIEW_ACTION.  The recorder owns the heap-allocated instance.
     */
    class Recorder
    {
    public:
        explicit Recorder(AbstractReviewAction *action);
    };

private:
    static QList<AbstractReviewAction *> &getActions();
};

/**
 * Place at file scope in the .cpp of each concrete review action.
 * The macro creates a static Recorder that registers the action at startup.
 */
#define DECLARE_REVIEW_ACTION(ClassName)                          \
    AbstractReviewAction::Recorder recorder##ClassName {          \
        new ClassName                                             \
    };

#endif // ABSTRACTREVIEWACTION_H
