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
