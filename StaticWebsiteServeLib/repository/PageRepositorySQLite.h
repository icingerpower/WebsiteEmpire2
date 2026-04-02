#pragma once

#include "repository/IPageRepository.h"
#include "db/ContentDb.h"

class PageRepositorySQLite : public IPageRepository
{
public:
    explicit PageRepositorySQLite(ContentDb &db);

    std::optional<PageRecord>        findByPath(const std::string &path) const override;
    std::optional<PageVariantRecord> findVariant(int64_t pageId, const std::string &label) const override;
    std::vector<std::string>         findActiveVariantLabels(int64_t pageId) const override;

private:
    ContentDb &m_db;
};
