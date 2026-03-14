# Qt / C++ Rules

## Exception Handling
- **DO NOT SILENCE EXCEPTIONS**: Never catch an exception and return a default/fallback value silently. This leads to corrupted data and silent failures.
  - **BAD**: Catching an exception, logging a warning, and returning `0`, `nullptr`, or an empty value.
  - **GOOD**: Let the exception propagate so the operation fails explicitly, or handle it by notifying the user immediately.

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
