#pragma once

#include "repository/IRedirectRepository.h"
#include "db/ContentDb.h"

class RedirectRepositorySQLite : public IRedirectRepository
{
public:
    explicit RedirectRepositorySQLite(ContentDb &db);

    std::optional<RedirectRecord> findByPath(const std::string &path) const override;

private:
    ContentDb &m_db;
};
