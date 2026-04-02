#include <thread>

#include <drogon/drogon.h>

#include "controllers/ImageController.h"
#include "controllers/PageController.h"
#include "controllers/StatsController.h"
#include "db/ContentDb.h"
#include "db/ImageDb.h"
#include "db/StatsDb.h"
#include "repository/ImageRepositorySQLite.h"
#include "repository/MenuRepositorySQLite.h"
#include "repository/PageRepositorySQLite.h"
#include "repository/RedirectRepositorySQLite.h"
#include "repository/StatsWriterSQLite.h"

int main()
{
    // TODO: load paths from a config file or CLI arguments.
    ContentDb contentDb("content.db");
    ImageDb   imageDb("images.db");
    StatsDb   statsDb("stats.db");

    PageRepositorySQLite    pageRepo(contentDb);
    MenuRepositorySQLite    menuRepo(contentDb);
    RedirectRepositorySQLite redirectRepo(contentDb);
    ImageRepositorySQLite   imageRepo(imageDb);
    StatsWriterSQLite       statsWriter(statsDb);

    // Inject dependencies before app().run() — controllers are auto-created by Drogon.
    PageController::setPageRepository(&pageRepo);
    PageController::setMenuRepository(&menuRepo);
    PageController::setRedirectRepository(&redirectRepo);
    PageController::loadMenuCache(&menuRepo);  // populate in-memory menu cache

    ImageController::setImageRepository(&imageRepo);
    StatsController::setStatsWriter(&statsWriter);

    drogon::app()
        .addListener("0.0.0.0", 8080)
        .setThreadNum(static_cast<int>(std::thread::hardware_concurrency()))
        .run();

    return 0;
}
