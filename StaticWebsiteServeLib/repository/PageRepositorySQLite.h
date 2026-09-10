#pragma once

#include <string>

#include "repository/IPageRepository.h"
#include "db/ContentDb.h"

/**
 * IPageRepository backed by a content.db SQLite file.
 *
 * findByPath() matches the stored path exactly, then falls back to resolving a
 * directory-style request ("/", "/es/") to its index document.
 *
 * Language prefixes are deliberately NOT handled here: stripping them is
 * PageController::setPathPrefix()'s job, driven by the server's --path-prefix
 * option, because only the caller knows which language this instance serves.
 * Inferring it from the content would be a guess, and having two layers strip
 * prefixes would make the behaviour impossible to reason about.
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
     * Resolves a directory-style request ("/", "/es/") to its index document by
     * appending "index.html". Returns nullopt for any path not ending in '/'.
     *
     * PageGenerator writes the home page as "/index.html" and never as "/", so
     * a request for a language root ("/es/", which PageController reduces to
     * "/" once it strips the prefix) had nothing to match. nginx serves the
     * index document itself in production, which is why only local deploys were
     * affected.
     */
    std::optional<PageRecord> _selectDirectoryIndex(const std::string &path) const;

    ContentDb &m_db;
};
