---
name: WebsiteEmpire test target naming convention
description: All WebsiteEmpireTests CMake targets and C++ class names must start with Test_Website_
type: feedback
originSessionId: 4c1a1e44-2f55-4937-9df6-f5ead1ed38c2
---
All `WebsiteEmpireTests` CMake targets (and their matching C++ class names) must be prefixed `Test_Website_`.

**Why:** The ctest output shows target names; without the prefix there is no way to tell at a glance which project a test belongs to.

**How to apply:** When adding a new test to `WebsiteEmpireTests/CMakeLists.txt`, always use `Test_Website_<Subsystem>_<Thing>` as the target name and the identical string as the C++ class name inside the file. Same rule applies when renaming existing targets.
