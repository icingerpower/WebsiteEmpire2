#include "GeneratorLanguages.h"

#include "CountryLangManager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>

#include "aspire/attributes/languages/PageAttributesLangTag.h"
#include "aspire/attributes/languages/PageAttributesLangIdiom.h"
#include "aspire/attributes/languages/PageAttributesLangWord.h"
#include "aspire/downloader/DownloadedPagesTable.h"

DECLARE_GENERATOR(GeneratorLanguages)

const int GeneratorLanguages::MAX_TAGS_PER_JOB      = 30;
const int GeneratorLanguages::INITIAL_TAGS_PAGES    = 3;  // 3 × 30 = 90 tags pre-generated
const int GeneratorLanguages::IDIOMS_PER_WORD        = 10;
const int GeneratorLanguages::MISSING_WORDS_PER_LANG = 100;
const int GeneratorLanguages::MAX_CONTEXT_ITEMS      = 40; // max idioms/words sent per payload

const QString GeneratorLanguages::TASK_TAGS    = QStringLiteral("generate_tags");
const QString GeneratorLanguages::TASK_WORD_EN = QStringLiteral("tag_english_word");
const QString GeneratorLanguages::TASK_TRANS   = QStringLiteral("translate_word");
const QString GeneratorLanguages::TASK_MISSING = QStringLiteral("fill_missing_words");

GeneratorLanguages::GeneratorLanguages(const QDir        &workingDir,
                                       const QString     &wordsTxtPath,
                                       const QStringList &langCodes,
                                       QObject           *parent)
    : AbstractGenerator(workingDir, parent)
    , m_wordsTxtPath(wordsTxtPath)
    , m_langCodes(langCodes)
{
}

QString GeneratorLanguages::getId() const
{
    return QStringLiteral("language-learning");
}

QString GeneratorLanguages::getName() const
{
    return tr("Language Learning");
}

AbstractGenerator *GeneratorLanguages::createInstance(const QDir &workingDir) const
{
    return new GeneratorLanguages(workingDir);
}

// ---- Job-ID helpers --------------------------------------------------------

QString GeneratorLanguages::slugify(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar c : text) {
        if (c.isLetterOrNumber() && c.unicode() < 128) {
            result += c.toLower();
        } else if (c == QLatin1Char(' ') || c == QLatin1Char('_')) {
            result += QLatin1Char('-');
        } else if (c == QLatin1Char('-') || c == QLatin1Char('.')) {
            result += c;
        }
        // Other chars dropped.
    }
    return result;
}

QString GeneratorLanguages::tagsJobId(int page)
{
    return QStringLiteral("tags/") + QString::number(page);
}

QString GeneratorLanguages::wordEnJobId(const QString &wordSlug)
{
    return QStringLiteral("word/en/") + wordSlug;
}

QString GeneratorLanguages::transJobId(const QString &langCode, const QString &wordSlug)
{
    return QStringLiteral("trans/") + langCode + QLatin1Char('/') + wordSlug;
}

QString GeneratorLanguages::missingJobId(const QString &langCode)
{
    return QStringLiteral("missing/") + langCode;
}

bool GeneratorLanguages::isTagsJobId(const QString &jobId)
{
    const QStringList p = jobId.split(QLatin1Char('/'));
    return p.size() == 2 && p.at(0) == QLatin1String("tags");
}

bool GeneratorLanguages::isWordEnJobId(const QString &jobId)
{
    const QStringList p = jobId.split(QLatin1Char('/'));
    return p.size() == 3
           && p.at(0) == QLatin1String("word")
           && p.at(1) == QLatin1String("en");
}

bool GeneratorLanguages::isTransJobId(const QString &jobId)
{
    const QStringList p = jobId.split(QLatin1Char('/'));
    return p.size() == 3 && p.at(0) == QLatin1String("trans");
}

bool GeneratorLanguages::isMissingJobId(const QString &jobId)
{
    const QStringList p = jobId.split(QLatin1Char('/'));
    return p.size() == 2 && p.at(0) == QLatin1String("missing");
}

int GeneratorLanguages::pageFromTagsJobId(const QString &jobId)
{
    const QStringList p = jobId.split(QLatin1Char('/'));
    if (p.size() < 2) {
        return 0;
    }
    return p.at(1).toInt();
}

QString GeneratorLanguages::langCodeFromJobId(const QString &jobId)
{
    const QStringList p = jobId.split(QLatin1Char('/'));
    if (p.size() < 2) {
        return {};
    }
    return p.at(1); // "en" for word/en/<slug>, "fr" for trans/fr/<slug>
}

QString GeneratorLanguages::wordSlugFromJobId(const QString &jobId)
{
    const QStringList p = jobId.split(QLatin1Char('/'));
    if (p.size() < 3) {
        return {};
    }
    return p.at(2);
}


// ---- Word list loading ------------------------------------------------------

const QList<GeneratorLanguages::WordData> &GeneratorLanguages::words() const
{
    if (!m_wordsLoaded) {
        const QString path = m_wordsTxtPath.isEmpty()
                                 ? paramValue(QStringLiteral("words_txt_path")).toString()
                                 : m_wordsTxtPath;
        m_words      = loadWordsFromTxt(path);
        m_wordsLoaded = true;
    }
    return m_words;
}

QList<GeneratorLanguages::WordData> GeneratorLanguages::loadWordsFromTxt(const QString &path)
{
    QList<WordData> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "GeneratorLanguages: cannot open words file:" << path;
        return result;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    int rank = 1;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        // Support optional "rank word" or just "word" format.
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString word = (parts.size() >= 2 && parts.at(0).toInt() > 0)
                                 ? parts.at(1)
                                 : parts.at(0);
        if (word.isEmpty()) {
            continue;
        }
        WordData d;
        d.word = word;
        d.slug = slugify(word);
        d.rank = rank++;
        if (!d.slug.isEmpty()) {
            result << d;
        }
    }
    qDebug() << "GeneratorLanguages: loaded" << result.size() << "words from" << path;
    return result;
}

const GeneratorLanguages::WordData *GeneratorLanguages::findWord(const QString &slug) const
{
    for (const auto &w : words()) {
        if (w.slug == slug) {
            return &w;
        }
    }
    return nullptr;
}

QStringList GeneratorLanguages::targetLangCodes() const
{
    QStringList codes = m_langCodes.isEmpty()
                            ? paramValue(QStringLiteral("lang_codes"))
                                  .toString()
                                  .split(QLatin1Char(','), Qt::SkipEmptyParts)
                            : m_langCodes;
    if (codes.isEmpty()) {
        codes = CountryLangManager::instance()->defaultLangCodes();
    }
    // English is always the source language; never translate back to it.
    codes.removeAll(QStringLiteral("en"));
    return codes;
}

// ---- AbstractGenerator implementation ----------------------------------------

QStringList GeneratorLanguages::buildInitialJobIds() const
{
    QStringList ids;

    // Phase 1: pre-generate the full tag vocabulary before processing any words.
    // INITIAL_TAGS_PAGES pages × MAX_TAGS_PER_JOB tags each run sequentially,
    // so each page receives the prior pages' tags as existingTags and fills gaps.
    // processTagsReply() may still spawn further continuation pages if the last
    // page is full, but those go to the end of the queue as discovered jobs.
    for (int page = 0; page < INITIAL_TAGS_PAGES; ++page) {
        ids << tagsJobId(page);
    }

    // Phase 2: one word job per English word, ordered by rank (ascending).
    const auto &wordList = words();
    ids.reserve(1 + wordList.size() + targetLangCodes().size());
    for (const auto &w : wordList) {
        ids << wordEnJobId(w.slug);
    }

    // Phase 4: missing-word fill for each target language (runs after translations).
    for (const QString &lang : targetLangCodes()) {
        ids << missingJobId(lang);
    }

    return ids;
}

QJsonObject GeneratorLanguages::buildJobPayload(const QString &jobId) const
{
    if (isTagsJobId(jobId)) {
        return buildTagsPayload(jobId);
    }
    if (isWordEnJobId(jobId)) {
        return buildWordEnPayload(jobId);
    }
    if (isTransJobId(jobId)) {
        return buildTransPayload(jobId);
    }
    if (isMissingJobId(jobId)) {
        return buildMissingPayload(jobId);
    }
    return {};
}

// ---- Payload helpers --------------------------------------------------------

// Returns the last N items of list, capped at MAX_CONTEXT_ITEMS.
// Passing the most-recent items gives the AI enough context to avoid immediate
// duplicates without bloating the payload as the DB grows.
static QStringList cappedContext(const QStringList &list, int cap)
{
    if (list.size() <= cap) {
        return list;
    }
    return list.mid(list.size() - cap, cap);
}

// ---- Payload builders -------------------------------------------------------

QJsonObject GeneratorLanguages::buildTagsPayload(const QString &jobId) const
{
    const QStringList existing = loadExistingTags();
    QJsonArray existingArr;
    for (const QString &t : existing) {
        existingArr << t;
    }

    QJsonObject payload;
    payload[QStringLiteral("task")]         = TASK_TAGS;
    payload[QStringLiteral("existingTags")] = existingArr;
    payload[QStringLiteral("maxTags")]      = MAX_TAGS_PER_JOB;
    payload[QStringLiteral("instructions")] = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "Generate up to %1 language-learning tags not already in 'existingTags'. "
        "Tags should comprehensively cover themes (food, travel, body, nature, "
        "emotions…), grammar categories (noun, verb, pronoun, adjective…), "
        "everyday situations (shopping, restaurant, transport…), and word types. "
        "NEVER repeat a tag from 'existingTags'. "
        "Set 'percentage' to your estimate (0–100) of learners interested in this topic. "
        "Return fewer than %1 entries only when the vocabulary is genuinely exhausted.")
        .arg(MAX_TAGS_PER_JOB);

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")] = jobId;
    QJsonObject tagExample;
    tagExample[QStringLiteral("name")]       = QStringLiteral("food");
    tagExample[QStringLiteral("percentage")] = 85.0;
    replyFormat[QStringLiteral("tags")] = QJsonArray{tagExample};
    payload[QStringLiteral("replyFormat")] = replyFormat;

    return payload;
}

QJsonObject GeneratorLanguages::buildWordEnPayload(const QString &jobId) const
{
    const QString slug = wordSlugFromJobId(jobId);
    const WordData *w  = findWord(slug);
    if (!w) {
        return {};
    }

    const QStringList existingTags   = loadExistingTags();
    const QStringList recentIdioms   =
        cappedContext(loadExistingIdioms(QStringLiteral("en")), MAX_CONTEXT_ITEMS);

    QJsonArray tagsArr;
    for (const QString &t : existingTags) {
        tagsArr << t;
    }
    QJsonArray idiomsArr;
    for (const QString &ph : recentIdioms) {
        idiomsArr << ph;
    }

    QJsonObject payload;
    payload[QStringLiteral("task")]           = TASK_WORD_EN;
    payload[QStringLiteral("word")]           = w->word;
    payload[QStringLiteral("rank")]           = w->rank;
    payload[QStringLiteral("existingTags")]   = tagsArr;
    payload[QStringLiteral("existingIdioms")] = idiomsArr;
    payload[QStringLiteral("idiomsNeeded")]   = IDIOMS_PER_WORD;
    payload[QStringLiteral("instructions")]   = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "For the English word '%1' (frequency rank %2):\n"
        "1. 'tags': pick 1–4 tags from 'existingTags' that best describe this word. "
        "You may introduce a new tag name only if none of the existing ones fit; keep new "
        "tags short, lowercase, and in English.\n"
        "2. 'idioms': generate exactly %3 simple English phrases or short sentences that "
        "contain or illustrate this word. Prefer common, everyday phrases a beginner would "
        "encounter. NEVER repeat a phrase from 'existingIdioms'. Each idiom object needs "
        "'phrase' (the text) and 'tags' (1–3 tags from the same existingTags list).")
        .arg(w->word, QString::number(w->rank), QString::number(IDIOMS_PER_WORD));

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")] = jobId;
    replyFormat[QStringLiteral("tags")]  = QJsonArray{QStringLiteral("food")};
    QJsonObject idiomEx;
    idiomEx[QStringLiteral("phrase")] = QStringLiteral("An apple a day keeps the doctor away");
    idiomEx[QStringLiteral("tags")]   = QJsonArray{QStringLiteral("health")};
    replyFormat[QStringLiteral("idioms")] = QJsonArray{idiomEx};
    payload[QStringLiteral("replyFormat")] = replyFormat;

    return payload;
}

QJsonObject GeneratorLanguages::buildTransPayload(const QString &jobId) const
{
    const QString langCode = langCodeFromJobId(jobId);
    const QString slug     = wordSlugFromJobId(jobId);
    const WordData *w      = findWord(slug);
    if (!w || langCode.isEmpty()) {
        return {};
    }

    const QStringList existingTags  = loadExistingTags();
    const QStringList recentWords   =
        cappedContext(loadExistingWords(langCode), MAX_CONTEXT_ITEMS);
    const QStringList recentIdioms  =
        cappedContext(loadExistingIdioms(langCode), MAX_CONTEXT_ITEMS);

    QJsonArray tagsArr;
    for (const QString &t : existingTags) {
        tagsArr << t;
    }
    QJsonArray wordsArr;
    for (const QString &wt : recentWords) {
        wordsArr << wt;
    }
    QJsonArray idiomsArr;
    for (const QString &ph : recentIdioms) {
        idiomsArr << ph;
    }

    QJsonObject payload;
    payload[QStringLiteral("task")]            = TASK_TRANS;
    payload[QStringLiteral("sourceWord")]      = w->word;
    payload[QStringLiteral("sourceLang")]      = QStringLiteral("en");
    payload[QStringLiteral("targetLang")]      = langCode;
    payload[QStringLiteral("rank")]            = w->rank;
    payload[QStringLiteral("existingTags")]    = tagsArr;
    payload[QStringLiteral("existingWords")]   = wordsArr;
    payload[QStringLiteral("existingIdioms")]  = idiomsArr;
    payload[QStringLiteral("idiomsNeeded")]    = IDIOMS_PER_WORD;
    payload[QStringLiteral("instructions")]    = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "Translate the English word '%1' into language '%2' (BCP-47 code).\n"
        "1. 'translatedWord': the most common translation. If the word already appears "
        "in 'existingWords' (a previous translation mapped to it), use that exact form.\n"
        "2. 'tags': pick 1–4 tags from 'existingTags'. Introduce a new tag only if none fit.\n"
        "3. 'idioms': generate exactly %3 simple phrases in language '%2' that contain or "
        "illustrate this word. Prefer common, everyday phrases. "
        "NEVER repeat a phrase from 'existingIdioms'. "
        "Each idiom needs 'phrase' and 'tags' (1–3 tags from existingTags).")
        .arg(w->word, langCode, QString::number(IDIOMS_PER_WORD));

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")]          = jobId;
    replyFormat[QStringLiteral("translatedWord")] = QString{};
    replyFormat[QStringLiteral("tags")]           = QJsonArray{QStringLiteral("food")};
    QJsonObject idiomEx;
    idiomEx[QStringLiteral("phrase")] = QString{};
    idiomEx[QStringLiteral("tags")]   = QJsonArray{QStringLiteral("food")};
    replyFormat[QStringLiteral("idioms")] = QJsonArray{idiomEx};
    payload[QStringLiteral("replyFormat")] = replyFormat;

    return payload;
}

QJsonObject GeneratorLanguages::buildMissingPayload(const QString &jobId) const
{
    const QString langCode = langCodeFromJobId(jobId);
    if (langCode.isEmpty()) {
        return {};
    }

    const QStringList existingTags  = loadExistingTags();
    const QStringList allWords      = loadExistingWords(langCode);
    const QStringList recentIdioms  =
        cappedContext(loadExistingIdioms(langCode), MAX_CONTEXT_ITEMS);
    const int         currentMaxRank = maxWordRank(langCode);

    QJsonArray tagsArr;
    for (const QString &t : existingTags) {
        tagsArr << t;
    }
    // Pass ALL existing words so the AI knows exactly which words to skip.
    // (Words are shorter strings than idioms; this list stays manageable.)
    QJsonArray wordsArr;
    for (const QString &wt : allWords) {
        wordsArr << wt;
    }
    QJsonArray idiomsArr;
    for (const QString &ph : recentIdioms) {
        idiomsArr << ph;
    }

    QJsonObject payload;
    payload[QStringLiteral("task")]           = TASK_MISSING;
    payload[QStringLiteral("targetLang")]     = langCode;
    payload[QStringLiteral("existingTags")]   = tagsArr;
    payload[QStringLiteral("existingWords")]  = wordsArr;
    payload[QStringLiteral("existingIdioms")] = idiomsArr;
    payload[QStringLiteral("currentMaxRank")] = currentMaxRank;
    payload[QStringLiteral("wordsNeeded")]    = MISSING_WORDS_PER_LANG;
    payload[QStringLiteral("idiomsPerWord")]  = IDIOMS_PER_WORD;
    payload[QStringLiteral("instructions")]   = tr(
        "IMPORTANT: Reply ONLY with the raw JSON object shown in 'replyFormat' — "
        "no prose, no markdown, no text outside the JSON.\n\n"
        "For language '%1': provide up to %2 of the most common words in that language "
        "that are NOT already in 'existingWords'. These fill vocabulary gaps not covered "
        "by the English-sourced translations.\n"
        "For each word:\n"
        "  'word': the word in the target language.\n"
        "  'rank': frequency rank in the target language — assign values starting above %3.\n"
        "  'tags': 1–4 tags from 'existingTags' (introduce new only if none fit).\n"
        "  'idioms': exactly %4 simple phrases in '%1' using this word. "
        "NEVER repeat a phrase from 'existingIdioms'. Each idiom needs 'phrase' and 'tags'.")
        .arg(langCode, QString::number(MISSING_WORDS_PER_LANG),
             QString::number(currentMaxRank), QString::number(IDIOMS_PER_WORD));

    QJsonObject wordEx;
    wordEx[QStringLiteral("word")] = QString{};
    wordEx[QStringLiteral("rank")] = currentMaxRank + 1;
    wordEx[QStringLiteral("tags")] = QJsonArray{QStringLiteral("grammar")};
    QJsonObject idiomEx;
    idiomEx[QStringLiteral("phrase")] = QString{};
    idiomEx[QStringLiteral("tags")]   = QJsonArray{QStringLiteral("grammar")};
    wordEx[QStringLiteral("idioms")]  = QJsonArray{idiomEx};

    QJsonObject replyFormat;
    replyFormat[QStringLiteral("jobId")] = jobId;
    replyFormat[QStringLiteral("words")] = QJsonArray{wordEx};
    payload[QStringLiteral("replyFormat")] = replyFormat;

    return payload;
}

// ---- Reply processors -------------------------------------------------------

void GeneratorLanguages::processReply(const QString &jobId, const QJsonObject &reply)
{
    if (isTagsJobId(jobId)) {
        processTagsReply(jobId, reply);
    } else if (isWordEnJobId(jobId)) {
        processWordEnReply(jobId, reply);
    } else if (isTransJobId(jobId)) {
        processTransReply(jobId, reply);
    } else if (isMissingJobId(jobId)) {
        processMissingReply(jobId, reply);
    } else {
        qDebug() << "GeneratorLanguages: unknown job type for" << jobId;
    }
}

void GeneratorLanguages::processTagsReply(const QString &jobId, const QJsonObject &reply)
{
    const QStringList existing = loadExistingTags();
    QSet<QString> seenTags(existing.begin(), existing.end());

    const QJsonArray tags = reply.value(QStringLiteral("tags")).toArray();
    int inserted = 0;
    for (const QJsonValue &v : std::as_const(tags)) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject obj  = v.toObject();
        const QString     name = obj.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() || seenTags.contains(name)) {
            continue;
        }
        seenTags.insert(name);
        QHash<QString, QString> attrs;
        attrs.insert(PageAttributesLangTag::ID_NAME,       name);
        attrs.insert(PageAttributesLangTag::ID_PERCENTAGE,
                     QString::number(obj.value(QStringLiteral("percentage")).toDouble()));
        recordResultPage(QStringLiteral("PageAttributesLangTag"), attrs);
        ++inserted;
    }

    qDebug() << "GeneratorLanguages: tags job" << jobId
             << "— inserted" << inserted << "tag(s)";

    // Spawn continuation if the batch was full.
    if (tags.size() >= MAX_TAGS_PER_JOB) {
        addDiscoveredJob(tagsJobId(pageFromTagsJobId(jobId) + 1));
    }
}

void GeneratorLanguages::processWordEnReply(const QString &jobId, const QJsonObject &reply)
{
    const QString slug = wordSlugFromJobId(jobId);
    const WordData *w  = findWord(slug);
    if (!w) {
        qDebug() << "GeneratorLanguages: word not found for" << jobId;
        return;
    }

    // ---- tags: record any new ones -----------------------------------------
    const QStringList existingTags = loadExistingTags();
    QSet<QString> seenTags(existingTags.begin(), existingTags.end());
    const QJsonArray replyTags = reply.value(QStringLiteral("tags")).toArray();
    QStringList wordTags;
    for (const QJsonValue &v : std::as_const(replyTags)) {
        const QString tag = v.toString().trimmed();
        if (tag.isEmpty()) {
            continue;
        }
        if (!seenTags.contains(tag)) {
            seenTags.insert(tag);
            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesLangTag::ID_NAME,       tag);
            attrs.insert(PageAttributesLangTag::ID_PERCENTAGE, QStringLiteral("50"));
            recordResultPage(QStringLiteral("PageAttributesLangTag"), attrs);
        }
        wordTags << tag;
    }

    // ---- idioms: record new ones, collect phrase list for word record --------
    const QStringList existingIdioms = loadExistingIdioms(QStringLiteral("en"));
    QSet<QString> seenIdioms(existingIdioms.begin(), existingIdioms.end());
    const QString idiomsList =
        recordIdioms(reply.value(QStringLiteral("idioms")).toArray(),
                     QStringLiteral("en"), seenIdioms);

    // ---- word record --------------------------------------------------------
    QHash<QString, QString> wordAttrs;
    wordAttrs.insert(PageAttributesLangWord::ID_WORD,      w->word);
    wordAttrs.insert(PageAttributesLangWord::ID_RANK,      QString::number(w->rank));
    wordAttrs.insert(PageAttributesLangWord::ID_LANG_CODE, QStringLiteral("en"));
    wordAttrs.insert(PageAttributesLangWord::ID_TAGS,      wordTags.join(QLatin1Char(';')));
    wordAttrs.insert(PageAttributesLangWord::ID_IDIOMS,    idiomsList);
    recordResultPage(QStringLiteral("PageAttributesLangWord"), wordAttrs);

    qDebug() << "GeneratorLanguages: word/en" << w->word
             << "tags:" << wordTags.size() << "idioms:" << idiomsList.split(QLatin1Char(';')).size();

    // ---- spawn translation jobs --------------------------------------------
    for (const QString &lang : targetLangCodes()) {
        addDiscoveredJob(transJobId(lang, slug));
    }
}

void GeneratorLanguages::processTransReply(const QString &jobId, const QJsonObject &reply)
{
    const QString langCode = langCodeFromJobId(jobId);
    const QString slug     = wordSlugFromJobId(jobId);
    const WordData *w      = findWord(slug);
    if (!w || langCode.isEmpty()) {
        return;
    }

    const QString translatedWord =
        reply.value(QStringLiteral("translatedWord")).toString().trimmed();
    if (translatedWord.isEmpty()) {
        qDebug() << "GeneratorLanguages: empty translatedWord for" << jobId;
        return;
    }

    // Guard: another English word may already have mapped to the same translation.
    const QStringList existingWords = loadExistingWords(langCode);
    if (existingWords.contains(translatedWord)) {
        qDebug() << "GeneratorLanguages: skipping duplicate translated word"
                 << translatedWord << "for" << langCode;
        return;
    }

    // ---- tags ---------------------------------------------------------------
    const QStringList existingTags = loadExistingTags();
    QSet<QString> seenTags(existingTags.begin(), existingTags.end());
    const QJsonArray replyTags = reply.value(QStringLiteral("tags")).toArray();
    QStringList wordTags;
    for (const QJsonValue &v : std::as_const(replyTags)) {
        const QString tag = v.toString().trimmed();
        if (tag.isEmpty()) {
            continue;
        }
        if (!seenTags.contains(tag)) {
            seenTags.insert(tag);
            QHash<QString, QString> attrs;
            attrs.insert(PageAttributesLangTag::ID_NAME,       tag);
            attrs.insert(PageAttributesLangTag::ID_PERCENTAGE, QStringLiteral("50"));
            recordResultPage(QStringLiteral("PageAttributesLangTag"), attrs);
        }
        wordTags << tag;
    }

    // ---- idioms -------------------------------------------------------------
    const QStringList existingIdioms = loadExistingIdioms(langCode);
    QSet<QString> seenIdioms(existingIdioms.begin(), existingIdioms.end());
    const QString idiomsList =
        recordIdioms(reply.value(QStringLiteral("idioms")).toArray(),
                     langCode, seenIdioms);

    // ---- word record --------------------------------------------------------
    QHash<QString, QString> wordAttrs;
    wordAttrs.insert(PageAttributesLangWord::ID_WORD,      translatedWord);
    wordAttrs.insert(PageAttributesLangWord::ID_RANK,      QString::number(w->rank));
    wordAttrs.insert(PageAttributesLangWord::ID_LANG_CODE, langCode);
    wordAttrs.insert(PageAttributesLangWord::ID_TAGS,      wordTags.join(QLatin1Char(';')));
    wordAttrs.insert(PageAttributesLangWord::ID_IDIOMS,    idiomsList);
    recordResultPage(QStringLiteral("PageAttributesLangWord"), wordAttrs);

    qDebug() << "GeneratorLanguages: trans" << langCode << w->word << "->" << translatedWord;
}

void GeneratorLanguages::processMissingReply(const QString &jobId, const QJsonObject &reply)
{
    const QString langCode = langCodeFromJobId(jobId);
    if (langCode.isEmpty()) {
        return;
    }

    const QStringList existingTags  = loadExistingTags();
    QSet<QString> seenTags(existingTags.begin(), existingTags.end());

    const QStringList existingWords = loadExistingWords(langCode);
    QSet<QString> seenWords(existingWords.begin(), existingWords.end());

    const QStringList existingIdioms = loadExistingIdioms(langCode);
    QSet<QString> seenIdioms(existingIdioms.begin(), existingIdioms.end());

    const QJsonArray wordsArr = reply.value(QStringLiteral("words")).toArray();
    int inserted = 0;
    for (const QJsonValue &v : std::as_const(wordsArr)) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject wObj = v.toObject();
        const QString word = wObj.value(QStringLiteral("word")).toString().trimmed();
        if (word.isEmpty() || seenWords.contains(word)) {
            continue;
        }
        seenWords.insert(word);

        // Tags
        QStringList wordTags;
        const QJsonArray wTags = wObj.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &tv : std::as_const(wTags)) {
            const QString tag = tv.toString().trimmed();
            if (tag.isEmpty()) {
                continue;
            }
            if (!seenTags.contains(tag)) {
                seenTags.insert(tag);
                QHash<QString, QString> attrs;
                attrs.insert(PageAttributesLangTag::ID_NAME,       tag);
                attrs.insert(PageAttributesLangTag::ID_PERCENTAGE, QStringLiteral("50"));
                recordResultPage(QStringLiteral("PageAttributesLangTag"), attrs);
            }
            wordTags << tag;
        }

        // Idioms
        const QString idiomsList =
            recordIdioms(wObj.value(QStringLiteral("idioms")).toArray(),
                         langCode, seenIdioms);

        QHash<QString, QString> wordAttrs;
        wordAttrs.insert(PageAttributesLangWord::ID_WORD,
                         word);
        wordAttrs.insert(PageAttributesLangWord::ID_RANK,
                         QString::number(wObj.value(QStringLiteral("rank")).toInt()));
        wordAttrs.insert(PageAttributesLangWord::ID_LANG_CODE, langCode);
        wordAttrs.insert(PageAttributesLangWord::ID_TAGS,
                         wordTags.join(QLatin1Char(';')));
        wordAttrs.insert(PageAttributesLangWord::ID_IDIOMS, idiomsList);
        recordResultPage(QStringLiteral("PageAttributesLangWord"), wordAttrs);
        ++inserted;
    }

    qDebug() << "GeneratorLanguages: missing" << langCode
             << "— inserted" << inserted << "word(s)";
}

// ---- Shared idiom recorder --------------------------------------------------

QString GeneratorLanguages::recordIdioms(const QJsonArray &idiomsArr,
                                          const QString    &langCode,
                                          QSet<QString>    &seenIdioms)
{
    const QStringList existingTags = loadExistingTags();
    QSet<QString> seenTags(existingTags.begin(), existingTags.end());

    QStringList phrases;
    for (const QJsonValue &v : std::as_const(idiomsArr)) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject obj    = v.toObject();
        const QString     phrase = obj.value(QStringLiteral("phrase")).toString().trimmed();
        if (phrase.isEmpty() || seenIdioms.contains(phrase)) {
            continue;
        }
        seenIdioms.insert(phrase);

        QStringList idiomTags;
        const QJsonArray iTags = obj.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &tv : std::as_const(iTags)) {
            const QString tag = tv.toString().trimmed();
            if (tag.isEmpty()) {
                continue;
            }
            if (!seenTags.contains(tag)) {
                seenTags.insert(tag);
                QHash<QString, QString> attrs;
                attrs.insert(PageAttributesLangTag::ID_NAME,       tag);
                attrs.insert(PageAttributesLangTag::ID_PERCENTAGE, QStringLiteral("50"));
                recordResultPage(QStringLiteral("PageAttributesLangTag"), attrs);
            }
            idiomTags << tag;
        }

        QHash<QString, QString> idiomAttrs;
        idiomAttrs.insert(PageAttributesLangIdiom::ID_PHRASE,    phrase);
        idiomAttrs.insert(PageAttributesLangIdiom::ID_LANG_CODE, langCode);
        idiomAttrs.insert(PageAttributesLangIdiom::ID_TAGS,      idiomTags.join(QLatin1Char(';')));
        recordResultPage(QStringLiteral("PageAttributesLangIdiom"), idiomAttrs);

        phrases << phrase;
    }
    return phrases.join(QLatin1Char(';'));
}

// ---- DB helpers ------------------------------------------------------------

// Helper to run a single-column SELECT on a QSqlDatabase, with an optional
// equality filter.
static QStringList runColumnQuery(QSqlDatabase       &db,
                                  const QString       &column,
                                  const QString       &filterColumn = QString(),
                                  const QString       &filterValue  = QString())
{
    QStringList results;
    QSqlQuery q(db);
    if (!filterColumn.isEmpty()) {
        q.prepare(QStringLiteral("SELECT %1 FROM records WHERE %2 = ?")
                  .arg(column, filterColumn));
        q.addBindValue(filterValue);
    } else {
        q.prepare(QStringLiteral("SELECT %1 FROM records").arg(column));
    }
    if (q.exec()) {
        while (q.next()) {
            const QString val = q.value(0).toString();
            if (!val.isEmpty()) {
                results << val;
            }
        }
    }
    return results;
}

// Opens a temporary SQLite connection, runs func(db), then removes it.
// Returns func's result, or an empty QStringList on open failure.
template<typename Func>
static QStringList withTempDb(const QString &dbPath,
                               const QString &connPrefix,
                               Func           func)
{
    if (!QFile::exists(dbPath)) {
        return {};
    }
    const QString connName =
        connPrefix + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QStringList result;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            result = func(db);
        }
    } // db destroyed here — SQLite lock released
    QSqlDatabase::removeDatabase(connName);
    return result;
}

QStringList GeneratorLanguages::loadExistingTags() const
{
    const DownloadedPagesTable *table =
        resultsTable(QStringLiteral("PageAttributesLangTag"));
    if (table) {
        QSqlDatabase db = table->database();
        return runColumnQuery(db, PageAttributesLangTag::ID_NAME);
    }
    const QString dbPath = workingDir().filePath(
        QStringLiteral("results_db/PageAttributesLangTag.db"));
    return withTempDb(dbPath, getId() + QLatin1String("_tags"), [](QSqlDatabase &db) {
        return runColumnQuery(db, PageAttributesLangTag::ID_NAME);
    });
}

QStringList GeneratorLanguages::loadExistingWords(const QString &langCode) const
{
    const DownloadedPagesTable *table =
        resultsTable(QStringLiteral("PageAttributesLangWord"));
    if (table) {
        QSqlDatabase db = table->database();
        return runColumnQuery(db, PageAttributesLangWord::ID_WORD,
                              PageAttributesLangWord::ID_LANG_CODE, langCode);
    }
    const QString dbPath = workingDir().filePath(
        QStringLiteral("results_db/PageAttributesLangWord.db"));
    return withTempDb(dbPath, getId() + QLatin1String("_words"),
                      [&langCode](QSqlDatabase &db) {
                          return runColumnQuery(db, PageAttributesLangWord::ID_WORD,
                                               PageAttributesLangWord::ID_LANG_CODE,
                                               langCode);
                      });
}

QStringList GeneratorLanguages::loadExistingIdioms(const QString &langCode) const
{
    const DownloadedPagesTable *table =
        resultsTable(QStringLiteral("PageAttributesLangIdiom"));
    if (table) {
        QSqlDatabase db = table->database();
        return runColumnQuery(db, PageAttributesLangIdiom::ID_PHRASE,
                              PageAttributesLangIdiom::ID_LANG_CODE, langCode);
    }
    const QString dbPath = workingDir().filePath(
        QStringLiteral("results_db/PageAttributesLangIdiom.db"));
    return withTempDb(dbPath, getId() + QLatin1String("_idioms"),
                      [&langCode](QSqlDatabase &db) {
                          return runColumnQuery(db, PageAttributesLangIdiom::ID_PHRASE,
                                               PageAttributesLangIdiom::ID_LANG_CODE,
                                               langCode);
                      });
}

int GeneratorLanguages::maxWordRank(const QString &langCode) const
{
    auto queryMax = [&langCode](QSqlDatabase &db) -> QStringList {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT MAX(CAST(%1 AS INTEGER)) FROM records WHERE %2 = ?")
                  .arg(PageAttributesLangWord::ID_RANK,
                       PageAttributesLangWord::ID_LANG_CODE));
        q.addBindValue(langCode);
        if (q.exec() && q.next()) {
            return {q.value(0).toString()};
        }
        return {};
    };

    const DownloadedPagesTable *table =
        resultsTable(QStringLiteral("PageAttributesLangWord"));
    QStringList res;
    if (table) {
        QSqlDatabase db = table->database();
        res = queryMax(db);
    } else {
        const QString dbPath = workingDir().filePath(
            QStringLiteral("results_db/PageAttributesLangWord.db"));
        res = withTempDb(dbPath, getId() + QLatin1String("_rank"), queryMax);
    }
    return res.isEmpty() ? 0 : res.first().toInt();
}

// ---- Parameters ------------------------------------------------------------

QList<AbstractGenerator::Param> GeneratorLanguages::getParams() const
{
    Param words;
    words.id           = QStringLiteral("words_txt_path");
    words.name         = tr("English Words File");
    words.tooltip      = tr("Plain-text file with the most common English words, one per line "
                            "(rank = line number). Lines starting with '#' are ignored.");
    words.defaultValue = QString{};
    words.isFile       = true;

    Param langs;
    langs.id           = QStringLiteral("lang_codes");
    langs.name         = tr("Target Language Codes");
    langs.tooltip      = tr("Comma-separated BCP-47 codes of the languages to generate data for "
                            "(e.g. \"fr,de,es\"). Defaults to the 40 most-spoken languages.");
    langs.defaultValue = CountryLangManager::instance()->defaultLangCodes().join(QLatin1Char(','));

    return {words, langs};
}

QString GeneratorLanguages::checkParams(const QList<Param> &params) const
{
    for (const Param &p : params) {
        if (p.id == QStringLiteral("words_txt_path")) {
            const QString path = p.defaultValue.toString();
            if (path.isEmpty()) {
                return tr("English Words File path is required.");
            }
            if (!QFile::exists(path)) {
                return tr("English Words File not found: %1").arg(path);
            }
        }
    }
    return {};
}

void GeneratorLanguages::onParamChanged(const QString &id)
{
    if (id == QStringLiteral("words_txt_path") || id == QStringLiteral("lang_codes")) {
        m_wordsLoaded = false;
        m_words.clear();
        if (!m_langCodes.isEmpty()) {
            m_langCodes.clear();
        }
        resetState();
    }
}

// ---- Result tables ---------------------------------------------------------

QMap<QString, AbstractPageAttributes *> GeneratorLanguages::createResultPageAttributes() const
{
    return {
        {tr("Language Tags"),   new PageAttributesLangTag()},
        {tr("Language Idioms"), new PageAttributesLangIdiom()},
        {tr("Language Words"),  new PageAttributesLangWord()},
    };
}
