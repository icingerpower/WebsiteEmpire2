#include <QtTest>

#include "website/pages/IPageRepository.h"
#include "website/pages/PageFlag.h"
#include "website/pages/PageGenerationState.h"
#include "website/pages/PageRecord.h"
#include "website/pages/PermalinkHistoryEntry.h"
#include "website/review/AbstractReviewAction.h"
#include "website/review/ReviewActionSocialMedia.h"

// ---------------------------------------------------------------------------
// SpyPageRepository — records setFlag() calls; all else is a no-op stub
// ---------------------------------------------------------------------------

namespace {

struct SetFlagCall {
    int      id;
    PageFlag flag;
    bool     on;
};

class SpyPageRepository : public IPageRepository
{
public:
    QList<SetFlagCall> flagCalls;

    void setFlag(int id, PageFlag flag, bool on) override
    {
        flagCalls.append({id, flag, on});
    }

    // Unused stubs
    int create(const QString &, const QString &, const QString &) override { return 0; }
    int createTranslation(int, const QString &, const QString &, const QString &) override { return 0; }
    std::optional<PageRecord> findById(int) const override { return {}; }
    QList<PageRecord> findAll() const override { return {}; }
    QList<PageRecord> findSourcePages(const QString &) const override { return {}; }
    QList<PageRecord> findTranslations(int) const override { return {}; }
    QHash<QString, QString> loadData(int) const override { return {}; }
    void saveData(int, const QHash<QString, QString> &) override {}
    void remove(int) override {}
    void updatePermalink(int, const QString &) override {}
    QList<PermalinkHistoryEntry> permalinkHistory(int) const override { return {}; }
    void setHistoryRedirectType(int, const QString &) override {}
    QString translatedAt(int) const override { return {}; }
    void setTranslatedAt(int, const QString &) override {}
    QList<PageRecord> findPendingByTypeId(const QString &) const override { return {}; }
    int countByTypeId(const QString &) const override { return 0; }
    void setGeneratedAt(int, const QString &) override {}
    void recordStrategyAttempt(int, const QString &) override {}
    QStringList strategyAttempts(int) const override { return {}; }
    QList<PageRecord> findGeneratedByTypeId(const QString &) const override { return {}; }
    void recordUpdateAttempt(int, const QString &) override {}
    QString lastUpdateAttemptAt(int, const QString &) const override { return {}; }
    QList<PageRecord> findPagesForUpdate(const QString &, const QString &, int, const QString &) const override { return {}; }
    QList<PageRecord> findPagesWithUpdateAttempt(const QString &) const override { return {}; }
    QList<PageRecord> findPagesWithoutUpdateAttempt(const QString &, const QString &) const override { return {}; }
    void clearUpdateAttempts(const QList<int> &, const QString &) override {}
    void setLangCodesToTranslate(int, const QStringList &) override {}
    void clearTranslationData(int, const QString &) override {}
    void clearAllTranslationData(int) override {}
    void setGenerationState(int, PageGenerationState) override {}
    QList<PageRecord> findByGenerationState(const QString &, PageGenerationState) const override { return {}; }
    PageGenerationState translationImageState(int, const QString &) const override { return PageGenerationState::Pending; }
    void setTranslationImageState(int, const QString &, PageGenerationState) override {}
    void invalidateTranslationImages(int) override {}
    QStringList pendingTranslationImageLangs(int) const override { return {}; }
    QList<PageRecord> findByFlag(PageFlag) const override { return {}; }
    void setEndPermalink(int, const QString &) override {}
    void setPublishedAt(int, const QString &) override {}
    void markAllCompleteAsPublished() override {}
};

PageRecord makeSourcePage(int id, const QString &typeId, quint32 flags = 0)
{
    PageRecord r;
    r.id       = id;
    r.typeId   = typeId;
    r.permalink = QStringLiteral("/p.html");
    r.lang     = QStringLiteral("en");
    r.flags    = flags;
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Website_ReviewAction : public QObject
{
    Q_OBJECT

private slots:
    // --- AbstractReviewAction registry ---
    void test_reviewaction_registry_not_empty();
    void test_reviewaction_socialmedia_id_not_empty();
    void test_reviewaction_socialmedia_displayname_not_empty();

    // --- ReviewActionSocialMedia::run() ---
    void test_reviewaction_socialmedia_run_already_flagged_returns_false();
    void test_reviewaction_socialmedia_run_unknown_type_returns_false();
    void test_reviewaction_socialmedia_run_below_threshold_returns_false();
    void test_reviewaction_socialmedia_run_at_threshold_sets_flag_returns_true();
    void test_reviewaction_socialmedia_run_above_threshold_sets_flag_returns_true();
    void test_reviewaction_socialmedia_run_sets_flag_on_correct_page_id();
};

// ---------------------------------------------------------------------------
// AbstractReviewAction registry
// ---------------------------------------------------------------------------

void Test_Website_ReviewAction::test_reviewaction_registry_not_empty()
{
    QVERIFY(!AbstractReviewAction::all().isEmpty());
}

void Test_Website_ReviewAction::test_reviewaction_socialmedia_id_not_empty()
{
    ReviewActionSocialMedia action;
    QVERIFY(!action.getId().isEmpty());
}

void Test_Website_ReviewAction::test_reviewaction_socialmedia_displayname_not_empty()
{
    ReviewActionSocialMedia action;
    QVERIFY(!action.getDisplayName().isEmpty());
}

// ---------------------------------------------------------------------------
// ReviewActionSocialMedia::run()
// ---------------------------------------------------------------------------

void Test_Website_ReviewAction::test_reviewaction_socialmedia_run_already_flagged_returns_false()
{
    ReviewActionSocialMedia action;
    SpyPageRepository spy;
    const PageRecord page = makeSourcePage(1, QStringLiteral("article"),
                                           static_cast<quint32>(PageFlag::SocialMedia));
    QCOMPARE(action.run(page, 200, spy), false);
    QVERIFY(spy.flagCalls.isEmpty());
}

void Test_Website_ReviewAction::test_reviewaction_socialmedia_run_unknown_type_returns_false()
{
    ReviewActionSocialMedia action;
    SpyPageRepository spy;
    const PageRecord page = makeSourcePage(1, QStringLiteral("nonexistent-type-id"));
    QCOMPARE(action.run(page, 200, spy), false);
    QVERIFY(spy.flagCalls.isEmpty());
}

void Test_Website_ReviewAction::test_reviewaction_socialmedia_run_below_threshold_returns_false()
{
    // PageTypeArticle's PageBlocSocialMedia has threshold 100.
    ReviewActionSocialMedia action;
    SpyPageRepository spy;
    const PageRecord page = makeSourcePage(1, QStringLiteral("article"));
    QCOMPARE(action.run(page, 99, spy), false);
    QVERIFY(spy.flagCalls.isEmpty());
}

void Test_Website_ReviewAction::test_reviewaction_socialmedia_run_at_threshold_sets_flag_returns_true()
{
    ReviewActionSocialMedia action;
    SpyPageRepository spy;
    const PageRecord page = makeSourcePage(1, QStringLiteral("article"));
    QCOMPARE(action.run(page, 100, spy), true);
    QCOMPARE(spy.flagCalls.size(), 1);
    QCOMPARE(spy.flagCalls.at(0).flag, PageFlag::SocialMedia);
    QCOMPARE(spy.flagCalls.at(0).on, true);
}

void Test_Website_ReviewAction::test_reviewaction_socialmedia_run_above_threshold_sets_flag_returns_true()
{
    ReviewActionSocialMedia action;
    SpyPageRepository spy;
    const PageRecord page = makeSourcePage(1, QStringLiteral("article"));
    QCOMPARE(action.run(page, 500, spy), true);
    QCOMPARE(spy.flagCalls.size(), 1);
}

void Test_Website_ReviewAction::test_reviewaction_socialmedia_run_sets_flag_on_correct_page_id()
{
    ReviewActionSocialMedia action;
    SpyPageRepository spy;
    const PageRecord page = makeSourcePage(42, QStringLiteral("article"));
    action.run(page, 100, spy);
    QCOMPARE(spy.flagCalls.at(0).id, 42);
}

QTEST_MAIN(Test_Website_ReviewAction)
#include "test_website_review_action.moc"
