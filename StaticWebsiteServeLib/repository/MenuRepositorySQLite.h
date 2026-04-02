#pragma once

#include "repository/IMenuRepository.h"
#include "db/ContentDb.h"

class MenuRepositorySQLite : public IMenuRepository
{
public:
    explicit MenuRepositorySQLite(ContentDb &db);

    std::optional<MenuFragmentRecord> findByDomainAndLang(const std::string &domain,
                                                           const std::string &lang) const override;
    std::vector<MenuFragmentRecord>   findAll() const override;

private:
    ContentDb &m_db;
};
