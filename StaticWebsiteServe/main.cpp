#include <cstdlib>
#include <filesystem>
#include <string_view>
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

int main(int argc, char *argv[])
{
    int port = 8080;
    std::string lang;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string_view(argv[i]) == "--port") {
            port = std::atoi(argv[i + 1]);
        } else if (std::string_view(argv[i]) == "--lang") {
            lang = argv[i + 1];
        }
    }

    ContentDb contentDb(ContentDb::FILENAME);
    ImageDb   imageDb(ImageDb::FILENAME);
    StatsDb   statsDb(StatsDb::FILENAME);

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

    // If --lang wasn't passed, derive from the working directory name.
    // The publish step starts each server inside deploy/<lang>/, so the
    // directory basename is the language code (e.g. "fr", "de").
    if (lang.empty()) {
        const std::string dirname =
            std::filesystem::current_path().filename().string();
        if (dirname.size() >= 2 && dirname.size() <= 5) {
            lang = dirname;
        }
    }
    if (!lang.empty()) {
        imageRepo.setLang(lang);
    }
    ImageController::setImageRepository(&imageRepo);
    StatsController::setStatsWriter(&statsWriter);

    drogon::app()
        .addListener("0.0.0.0", port)
        .setThreadNum(static_cast<int>(std::thread::hardware_concurrency()))
        .run();

    return 0;
}
