# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```bash
# Configure and build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run all tests (from build dir)
ctest

# Run a single test executable
./WebsiteAspireTests/Test_Aspired_Db
./WebsiteAspireTests/Test_Aspire_Page_Attributes
./WebsiteEmpireTests/Test_Website_PageBlocText
./StaticWebsiteServeTests/Test_StaticServe_Db
```

Dependencies: Qt6, QCoro6, Drogon, and a shared `../../common/` directory (sibling to this repo).
SQLiteCpp is fetched automatically by CMake FetchContent — no manual install needed.
Drogon must be built from source; see the install instructions in `StaticWebsiteServe/CMakeLists.txt`.

## Testing

### Test method naming
Each Qt test slot name must make the tested class and scenario immediately readable
without opening the file.  Use the pattern:

```
test_<shortcode_or_class>_<what_is_tested>
```

- **`test_video_`**, **`test_linkfix_`**, **`test_linktr_`** — one prefix per concrete class
- **`test_registry_`** — for behaviour that belongs to no single class (e.g. `forTag` with an unknown tag)
- Never use a generic prefix like `test_` alone when the test targets a specific class.

```cpp
// BAD  — impossible to tell which shortcode this covers at a glance
void test_for_tag_unknown_returns_nullptr();
void test_add_code_inner_content_in_html();

// GOOD
void test_registry_for_tag_unknown_returns_nullptr();
void test_linkfix_add_code_inner_content_in_html();
void test_linktr_add_code_inner_content_in_html();
```

### What to cover per shortcode
For every new shortcode, the test class must include slots for:
- Tag name (`getTag()`)
- Argument count, id, mandatory flag, allowedValues, translatability — one slot each
- Registry presence (`ALL_SHORTCODES`) and `forTag` round-trip
- `addCode` output: url/content appear in html, default argument values, css/js untouched, append behaviour
- `addCode` via `forTag` (exercises the full production path)
- Parse errors: mismatched tags, malformed input, duplicate argument
- Validation errors: missing mandatory argument, empty value, unknown argument

## Architecture

The project has two independent stacks:

**Qt stack (Qt6 + QCoro6):**

**`WebsiteEmpireLib/`** — static library shared by both Qt apps:
- `aspire/` — web scraping framework
- `website/` — website generation (early stage)
- `ExceptionWithTitleText` — custom Qt exception used throughout

**`WebsiteAspire/`** — GUI app for running scrapers and generating databases

**`WebsiteEmpire/`** — GUI app for building websites from scraped data

**Serve stack (Drogon + SQLiteCpp, Qt-free):**

**`StaticWebsiteServeLib/`** — Qt-free static library (pure C++/STL + SQLiteCpp):
- `db/` — `ContentDb`, `ImageDb`, `StatsDb` — open SQLite files and ensure schema
- `model/` — `PageRecord`, `ImageRecord` — plain value structs
- `repository/` — `IPageRepository`, `IImageRepository`, `IStatsWriter`, `IStatsReader` interfaces + `StatsWriterSQLite` implementation

**`StaticWebsiteServe/`** — Drogon HTTP server executable:
- `controllers/` — `PageController` (serves gzip HTML), `ImageController` (serves image blobs), `StatsController` (receives JS beacon POSTs)
- Drogon auto-creates controllers; inject dependencies via static setters before `app().run()`

**`StaticWebsiteServeTests/`** — Drogon test framework (`drogon_test.h`):
- Tests hit real temporary SQLite files — no mocking
- Use `CHECK(a == b)` for equality; Drogon has no `CHECK_EQ`

### Aspire Module

**`AbstractPageAttributes`** (QAbstractTableModel subclass) defines the schema for a page type. Each concrete subclass (e.g. `PageAttributesProductPetFood`) declares a list of `Attribute` structs with id, name, per-value validation lambda, and optional cross-validation. Subclasses self-register via the `DECLARE_PAGE_ATTRIBUTES()` macro into a static registry (`ALL_PAGE_ATTRIBUTES()`).

**`AbstractDownloader`** defines a web scraper plugin. Subclasses implement `getId()`, `getName()`, `getUrlsToParse()`, `getAttributeValues()`, and `fetchUrl()`. They self-register via `DECLARE_DOWNLOADER()`. The crawl loop runs as a QFuture continuation chain (non-blocking, event-loop based). State (visited URLs + pending queue) is persisted to `<getId()>.ini`.

**`AspiredDb`** manages an SQLite database per scraper. It creates tables from the scraper's `AbstractPageAttributes` schema (`createTableIdNeed()`), adds missing columns via `ALTER TABLE` on schema changes, and validates + inserts records (`record()`). Validation errors throw `ExceptionWithTitleText`.

### Generators with Multiple Result Tables (`createResultPageAttributes`)

`AbstractGenerator::createResultPageAttributes()` returns `QMap<QString, AbstractPageAttributes *>`:
- **Key** = human-readable display name wrapped in `tr()` — used only for the UI list. It is NOT stable and NOT persisted. Changing it is safe.
- **Value** = freshly heap-allocated `AbstractPageAttributes *` whose `getId()` is the stable DB filename stem (e.g. `"PageAttributesFactory"` → `PageAttributesFactory.db`). This ID must never change once data exists.

#### Recording values into a referenced table (no duplicates)

When one table references another (e.g. `PageAttributesFactory.category` → `PageAttributesFactoryCategory`), the referenced table acts as a controlled vocabulary. Follow this pattern in `processReply()`:

```cpp
// 1. Pre-populate seenValues from the ALREADY-OPEN model — never open a second
//    connection to the same .db file while a DownloadedPagesTable is open on it.
//    A second connection causes "database is locked" under SQLite journal mode.
const DownloadedPagesTable *refTable = resultsTable("PageAttributesRefTable");
QSet<QString> seenValues;
if (refTable) {
    const int col = refTable->fieldIndex(PageAttributesRefTable::ID_VALUE);
    for (int i = 0; i < refTable->rowCount(); ++i) {
        seenValues.insert(refTable->data(refTable->index(i, col)).toString());
    }
}

// 2. For each item in the reply, record it and add its referenced value only if new.
for (const QJsonValue &val : std::as_const(items)) {
    recordResultPage("PageAttributesMainTable", attrs);

    const QString refValue = itemObj.value("refField").toString().trimmed();
    if (!refValue.isEmpty() && !seenValues.contains(refValue)) {
        seenValues.insert(refValue);  // guard: only insert once per processReply AND across calls
        QHash<QString, QString> refAttrs;
        refAttrs.insert(PageAttributesRefTable::ID_VALUE, refValue);
        recordResultPage("PageAttributesRefTable", refAttrs);
    }
}
```

**Why pre-populate `seenValues` from the model instead of starting empty:** `processReply()` is called once per job. Without pre-population, every job re-inserts values that previous jobs already wrote, causing duplicates.

**Why use `resultsTable()` instead of opening a temporary `QSqlDatabase`:** `DownloadedPagesTable` already holds an open read connection to the `.db` file. Opening a second connection on the same file while the first is active causes `"database is locked"` errors when the write connection tries to INSERT. Always read from the already-open model.

**If you must open a temporary `QSqlDatabase`** (e.g. before `openResultsTable()` is called), ensure both the `QSqlQuery` and `QSqlDatabase` objects are destroyed in a nested scope **before** calling `QSqlDatabase::removeDatabase()`:
```cpp
QStringList values;
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    // ... open, query, fetch into values ...
} // db and query destroyed here — SQLite lock released
QSqlDatabase::removeDatabase(connName); // safe: no references remain
```
Calling `removeDatabase()` while a `QSqlDatabase` variable is still in scope triggers Qt's `"connection still in use"` warning and leaves a dangling SQLite read lock.

#### Constraining Claude to a fixed vocabulary

When a field must come from a fixed list (e.g. canonical category names), pass the list explicitly in the job payload and enforce it in the instruction:

```cpp
// In buildJobPayload():
const QStringList existing = loadExistingValues(); // reads from model (see above)
QJsonArray arr;
for (const QString &v : existing) { arr.append(v); }
payload["availableValues"] = arr;
payload["instructions"] = tr("... set the 'field' to the best match from "
    "'availableValues' — you MUST use one of those exact strings, no variations.");
```

Without this, Claude invents slight variations on each run (e.g. `"Pharmaceuticals & Biotechnology"` vs `"Pharmaceutical & Biotechnology Manufacturing"`), causing the referenced table to grow unboundedly with near-duplicates.

# Qt / C++ Rules

## Exception Handling
- **DO NOT SILENCE EXCEPTIONS**: Never catch an exception and return a default/fallback value silently. This leads to corrupted data and silent failures.
  - **BAD**: Catching an exception, logging a warning, and returning `0`, `nullptr`, or an empty value.
  - **GOOD**: Let the exception propagate so the operation fails explicitly, or handle it by notifying the user immediately.

## Working Directories
- Working directories come from exactly two sources: **UI user selection** or **`WorkingDirectoryManager::instance()->workingDir().path()`** (or a subdirectory of it).
- **Never** derive a working directory from Qt standard paths (`QStandardPaths`, `QDir::temp()`, `QDir::home()`, etc.).
- In production code, always obtain the working dir through one of the two approved sources above and pass it down explicitly.
- In tests, use `QTemporaryDir` as a self-contained working dir — this is the only acceptable exception.

## General Qt
- Use `QString` and `QList` over `std::string` and `std::vector` when interacting with Qt APIs.
- Prefer `qDebug()` for logging.

## Memory Management
- Use `QObject` parent-child hierarchy for automatic memory management.
- Use `QSharedPointer` or `QScopedPointer` for objects that are not QObjects.

## Test Integrity
- **DO NOT SUPPRESS EXCEPTIONS TO PASS TESTS**: If a test fails due to missing data, **DO NOT** catch the exception in the production code to make the test pass.
- **FIX THE ROOT CAUSE**: Update the test data or the data loading logic to include the missing data. A test failure is a valid signal that data is incomplete.

## Callback Pattern for Missing Data
When using a `callbackAddIfMissing` pattern:
1. Call the callback in a **while loop** as long as the data searched was not found OR the callback returns `true`
2. Only raise the exception if, after calling the callback:
   - The data searched is still not found, AND
   - `false` was returned (user cancelled/declined)

This pattern allows users to add missing configuration during operations without aborting.

## Code Style

### Braces
- **ALWAYS use braces** for control flow blocks, even single-line bodies:
  ```cpp
  // BAD
  if (cond) doSomething();

  // GOOD
  if (cond) {
      doSomething();
  }
  ```

### Const References for Temporaries
- Outside of coroutines, save temporary values as `const auto &` to avoid copies.
- **NEVER** chain calls on a single `const auto &` binding — split them to avoid dangling references:
  ```cpp
  // BAD
  const auto &temp = var.ret1().ret2();

  // GOOD
  const auto &temp1 = var.ret1();
  const auto &temp2 = temp1.ret2();
  ```

### QString::arg
- When calling `QString::arg` with more than one substitution, use the multi-argument overload:
  ```cpp
  // BAD
  str.arg(a).arg(b)

  // GOOD
  str.arg(a, b)
  ```

### Exception Raising
- Always raise exceptions using `ExceptionWithTitleText` with `.raise()`:
  ```cpp
  ExceptionWithTitleText ex("Invalid Inventory Move",
                            "transactionId cannot be empty");
  ex.raise();
  ```

### Translations
- Any user-visible string that is not a private/hidden ID must be wrapped with `tr()` or `QObject::tr()`.

## File Organisation

### Prefer .ui Files for Widget Configuration
- **Always prefer `.ui` files** over C++ code for configuring widget properties (selection mode, edit triggers, resize modes, stretch, alignment, etc.).
- Properties settable via Qt Designer XML must go in `.ui`, not in `.cpp`. Only use C++ for configuration that Qt Designer cannot express (e.g. `QHeaderView::setSectionResizeMode`, runtime model assignment, signal/slot connections).

### Implementations in .cpp
- Keep implementations in `.cpp` files. Only declare in `.h`.

### Header Documentation
- `.h` files must document non-obvious logic, invariants, and anything that has caused or could cause errors — not just signatures.

### Forward Declarations in Headers
- Prefer forward declarations (`class Foo;`) over includes in `.h` files; include the full header in the `.cpp`. This improves compilation performance.

## Performance & Modernness

### Warnings
- Code must compile without yellow warnings. Add `std::as_const` in range-for loops over non-const Qt containers when needed.

### Algorithms
- When Qt containers are compatible, prefer `std` algorithms (`std::find_if`, `std::transform`, etc.) over manual loops for performance and readability.

### AbstractShortCode::addCode — zero-copy rules
`addCode` is called once per shortcode occurrence per page and is on the hot path for
generating millions of pages. Every implementation must minimise allocations and copies:

- **Bind hash lookups with `const QString &`** — `QHash::value()` returns by value; capture
  the result as `const QString &` to avoid an extra copy:
  ```cpp
  // BAD  — copies the QString out of the hash
  const QString url = parsed.arguments.value(QStringLiteral("url"));

  // GOOD — reference into the hash's storage
  const QString &url = parsed.arguments.value(QStringLiteral("url"));
  ```
- **Use `QStringLiteral` for all string literals** — data lives in the binary; no heap
  allocation, no UTF-8→UTF-16 conversion at runtime. Do **not** wrap them in
  `static const QString`: `QStringLiteral` is already zero-cost.
  ```cpp
  // BAD  — heap-allocates on every call
  QString tag("<video src=\"");

  // BAD  — static allocation helps nothing; QStringLiteral needs no static wrapper
  static const QString tag = QStringLiteral("<video src=\"");

  // GOOD — compile-time data, stack wrapper, zero alloc
  html += QStringLiteral("<video src=\"");
  ```
- **Append in parts instead of using `.arg()`** — `.arg()` allocates a new `QString` for
  the substituted result. Multiple `+=` calls append directly into `html`'s existing buffer:
  ```cpp
  // BAD  — one extra heap allocation per call
  html += QStringLiteral("<video src=\"%1\" controls></video>").arg(url);

  // GOOD — zero intermediate allocations
  html += QStringLiteral("<video src=\"");
  html += url;
  html += QStringLiteral("\" controls></video>");
  ```
- **Check `cssDoneIds` / `jsDoneIds` before appending CSS or JS** — identical blocks must
  be emitted only once per page. Insert the block's id into the set on first write and
  skip if already present.
