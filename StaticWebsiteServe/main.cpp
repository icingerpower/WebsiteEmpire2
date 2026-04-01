#include <thread>

#include <drogon/drogon.h>

#include "controllers/ImageController.h"
#include "controllers/PageController.h"
#include "controllers/StatsController.h"
#include "db/ContentDb.h"
#include "db/ImageDb.h"
#include "db/StatsDb.h"
#include "repository/StatsWriterSQLite.h"

int main()
{
    // TODO: load paths from a config file or CLI arguments.
    ContentDb         contentDb("content.db");
    ImageDb           imageDb("images.db");
    StatsDb           statsDb("stats.db");
    StatsWriterSQLite statsWriter(statsDb);

    // Inject dependencies before app().run() — controllers are auto-created by Drogon.
    PageController::setContentDb(&contentDb);
    ImageController::setImageDb(&imageDb);
    StatsController::setStatsWriter(&statsWriter);

    drogon::app()
        .addListener("0.0.0.0", 8080)
        .setThreadNum(static_cast<int>(std::thread::hardware_concurrency()))
        .run();

    return 0;
}
