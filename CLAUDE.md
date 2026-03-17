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
./WebsiteEmpireTests/Test_Website_Blocks
```

Dependencies: Qt6, QCoro6, and a shared `../../common/` directory (sibling to this repo).

## Architecture

The project has three layers:

**`WebsiteEmpireLib/`** — static library shared by both apps:
- `aspire/` — web scraping framework
- `website/` — website generation (early stage)
- `ExceptionWithTitleText` — custom Qt exception used throughout

**`WebsiteAspire/`** — GUI app for running scrapers and generating databases

**`WebsiteEmpire/`** — GUI app for building websites from scraped data

### Aspire Module

**`AbstractPageAttributes`** (QAbstractTableModel subclass) defines the schema for a page type. Each concrete subclass (e.g. `PageAttributesProductPetFood`) declares a list of `Attribute` structs with id, name, per-value validation lambda, and optional cross-validation. Subclasses self-register via the `DECLARE_PAGE_ATTRIBUTES()` macro into a static registry (`ALL_PAGE_ATTRIBUTES()`).

**`AbstractDownloader`** defines a web scraper plugin. Subclasses implement `getId()`, `getName()`, `getUrlsToParse()`, `getAttributeValues()`, and `fetchUrl()`. They self-register via `DECLARE_DOWNLOADER()`. The crawl loop runs as a QFuture continuation chain (non-blocking, event-loop based). State (visited URLs + pending queue) is persisted to `<getId()>.ini`.

**`AspiredDb`** manages an SQLite database per scraper. It creates tables from the scraper's `AbstractPageAttributes` schema (`createTableIdNeed()`), adds missing columns via `ALTER TABLE` on schema changes, and validates + inserts records (`record()`). Validation errors throw `ExceptionWithTitleText`.

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
