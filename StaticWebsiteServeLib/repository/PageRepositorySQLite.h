#pragma once

#include <string>

#include "repository/IPageRepository.h"
#include "db/ContentDb.h"

/**
 * IPageRepository backed by a content.db SQLite file.
 *
 * findByPath() resolves a request path in two steps: an exact match first,
 * then — only on a miss — a retry with a leading "/<lang>" segment removed,
 * where <lang> is the language this content.db actually serves.
 *
 * Why that fallback exists: PageGenerator writes page paths WITHOUT a language
 * prefix ("/categories"), but since 7f10476 it renders link hrefs WITH one
 * ("/es/categories") so the links survive the nginx path-prefix proxy used for
 * non-primary languages in production. nginx strips "/es" before proxying, so
 * in production the server only ever sees the unprefixed path and the two
 * agree. A local deploy has no nginx, so every link on every non-primary
 * language page pointed at a path that did not exist — the site was reachable
 * but unbrowsable. Stripping here makes the server behave the same with or
 * without a proxy in front, so local browsing matches production exactly
 * rather than approximating it.
 *
 * The exact match is always preferred, so a real page whose own path genuinely
 * begins with the language code (e.g. "/es/guide") still wins over the
 * stripped interpretation.
 */
class PageRepositorySQLite : public IPageRepository
{
public:
    explicit PageRepositorySQLite(ContentDb &db);

    std::optional<PageRecord>        findByPath(const std::string &path) const override;
    std::optional<PageVariantRecord> findVariant(int64_t pageId, const std::string &label) const override;
    std::vector<std::string>         findActiveVariantLabels(int64_t pageId) const override;

private:
    /** Single exact "WHERE path = ?" lookup. Returns nullopt when absent. */
    std::optional<PageRecord> _selectByPath(const std::string &path) const;

    /**
     * The language this content.db serves: the most common lang among its page
     * rows, ignoring the "_" placeholder PageGenerator uses for a handful of
     * shared rows (robots.txt and friends). Resolved once on first use and
     * cached — a content.db is immutable for the lifetime of a served process.
     *
     * Returns an empty string for an empty database, which disables the
     * prefix fallback entirely rather than guessing.
     */
    const std::string &_servedLang() const;

    ContentDb          &m_db;
    mutable bool        m_servedLangResolved = false;
    mutable std::string m_servedLang;
};
