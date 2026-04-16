#ifndef GENERATORLANGUAGES_H
#define GENERATORLANGUAGES_H

#include "AbstractGenerator.h"

// Generator that builds a multilingual word and idiom database from a plain-text
// English word-frequency list.  Output feeds the language-learning website builder,
// which pairs languages (fr→en, en→fr, fr→de, …) and generates reading + exercise pages.
//
// --- Parameters ---
//   words_txt_path : file — plain-text file, one English word per line (rank = line number).
//   lang_codes     : string — comma-separated BCP-47 target-language codes.
//                    Default: 40 most-spoken languages, excluding English.
//
// --- Job pipeline ---
//   1. tags/<N>
//        Generates the tag vocabulary in batches of MAX_TAGS_PER_JOB.
//        Spawns tags/<N+1> when the batch is full.
//        Tags cover themes, grammar categories, situations, and word types.
//
//   2. word/en/<slug>            (initial — one per word in the file, ordered by rank)
//        Assigns tags to one English word and generates IDIOMS_PER_WORD English idioms.
//        Creates PageAttributesLangTag (new tags only), PageAttributesLangIdiom(en),
//        and PageAttributesLangWord(en).
//        Spawns trans/<langCode>/<slug> for every target language.
//
//   3. trans/<langCode>/<slug>   (discovered from word/en)
//        Translates one English word to the target language, assigns tags, and generates
//        IDIOMS_PER_WORD idioms in that language.
//        Creates PageAttributesLangIdiom(<lang>) and PageAttributesLangWord(<lang>).
//
//   4. missing/<langCode>        (initial — one per target language, at end of list)
//        Requests MISSING_WORDS_PER_LANG common words in the target language not yet
//        present in its word table.  Creates PageAttributesLangWord and
//        PageAttributesLangIdiom entries for each new word.
//
// Deduplication strategy — every payload includes the existing tags/words/idioms for
// the relevant language so the AI reuses them.  Every processReply() pre-populates a
// QSet from the DB before inserting, so residual duplicates from long replies are
// silently dropped.
class GeneratorLanguages : public AbstractGenerator
{
    Q_OBJECT

public:
    // Max tags returned per single tags/N job.  Spawns next page when hit.
    static const int MAX_TAGS_PER_JOB;
    // Number of tags/<N> jobs placed in the initial list so the full tag
    // vocabulary is available before any word/en/* job runs.
    static const int INITIAL_TAGS_PAGES;
    // Idioms generated alongside each word (English and each translation).
    static const int IDIOMS_PER_WORD;
    // Common words the AI is asked to add per missing/<lang> job.
    static const int MISSING_WORDS_PER_LANG;
    // Maximum number of existing idioms/words passed in a single payload for
    // context.  Keeps payloads small; the DB dedup handles any residual duplicates.
    static const int MAX_CONTEXT_ITEMS;

    static const QString TASK_TAGS;
    static const QString TASK_WORD_EN;
    static const QString TASK_TRANS;
    static const QString TASK_MISSING;

    explicit GeneratorLanguages(const QDir        &workingDir,
                                const QString     &wordsTxtPath = QString(),
                                const QStringList &langCodes    = QStringList(),
                                QObject           *parent       = nullptr);

    QString getId()   const override;
    QString getName() const override;
    AbstractGenerator *createInstance(const QDir &workingDir) const override;

    QList<Param>  getParams()                       const override;
    QString       checkParams(const QList<Param> &) const override;
    QMap<QString, AbstractPageAttributes *> createResultPageAttributes() const override;
    GeneratorTables getTables()                      const override;

    // ---- Job-ID helpers (public for testability) ----------------------------

    // Lower-case slug: letters/digits kept, spaces/underscores → '-', rest dropped.
    static QString slugify(const QString &text);

    static QString tagsJobId (int page);                                       // "tags/0"
    static QString wordEnJobId(const QString &wordSlug);                       // "word/en/apple"
    static QString transJobId (const QString &langCode, const QString &wordSlug); // "trans/fr/apple"
    static QString missingJobId(const QString &langCode);                      // "missing/fr"

    static bool isTagsJobId   (const QString &jobId); // "tags/<N>"
    static bool isWordEnJobId (const QString &jobId); // "word/en/<slug>"
    static bool isTransJobId  (const QString &jobId); // "trans/<lang>/<slug>"
    static bool isMissingJobId(const QString &jobId); // "missing/<lang>"

    // "tags/3" → 3
    static int pageFromTagsJobId(const QString &jobId);
    // "trans/fr/apple" or "word/en/apple" → "fr" / "en"
    static QString langCodeFromJobId(const QString &jobId);
    // "trans/fr/apple" or "word/en/apple" → "apple"
    static QString wordSlugFromJobId(const QString &jobId);


protected:
    QStringList buildInitialJobIds()                                            const override;
    QJsonObject buildJobPayload    (const QString &jobId)                       const override;
    void        processReply       (const QString &jobId, const QJsonObject &r)       override;
    void        onParamChanged     (const QString &id)                                override;

private:
    struct WordData {
        QString word; // original text from file
        QString slug; // slugify(word)
        int     rank; // 1-based line number
    };

    const QList<WordData> &words()                                const;
    static QList<WordData> loadWordsFromTxt(const QString &path);
    const WordData        *findWord(const QString &slug)          const;

    // Returns target language codes (from constructor override or param), sans "en".
    QStringList targetLangCodes() const;

    // ---- Payload builders ---------------------------------------------------
    QJsonObject buildTagsPayload   (const QString &jobId) const;
    QJsonObject buildWordEnPayload (const QString &jobId) const;
    QJsonObject buildTransPayload  (const QString &jobId) const;
    QJsonObject buildMissingPayload(const QString &jobId) const;

    // ---- Reply processors ---------------------------------------------------
    void processTagsReply   (const QString &jobId, const QJsonObject &reply);
    void processWordEnReply (const QString &jobId, const QJsonObject &reply);
    void processTransReply  (const QString &jobId, const QJsonObject &reply);
    void processMissingReply(const QString &jobId, const QJsonObject &reply);

    // ---- DB helpers ---------------------------------------------------------
    QStringList loadExistingTags()                       const;
    QStringList loadExistingWords(const QString &langCode)  const;
    QStringList loadExistingIdioms(const QString &langCode) const;
    int         maxWordRank(const QString &langCode)        const;

    // Records idioms from a JSON array into PageAttributesLangIdiom.
    // seenIdioms is pre-populated from the DB and updated as items are inserted.
    // Returns semicolon-joined phrase strings (used for the word's ID_IDIOMS field).
    QString recordIdioms(const QJsonArray &idiomsArr,
                         const QString    &langCode,
                         QSet<QString>    &seenIdioms);

    mutable QList<WordData> m_words;
    mutable bool            m_wordsLoaded = false;
    QString                 m_wordsTxtPath; // empty → read from param
    QStringList             m_langCodes;    // empty → read from param
};

#endif // GENERATORLANGUAGES_H
