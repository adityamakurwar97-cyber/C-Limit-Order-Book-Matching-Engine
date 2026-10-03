# Verification record

Verified in the Linux delivery environment on 3 October 2026. Mac commands are supplied but were not executed on a physical Mac.

## Completed

- C++17 build with GCC 13.3.0, -Wall -Wextra -Wpedantic.
- 11 correctness groups, including both-side priority, partial fills, IOC, FOK, market instructions, cancellation, numeric bounds and deterministic replay.
- 20,000 randomized events across four seeds compared against an independent vector reference. Trade records, outcomes and all active orders compared after every event; invariants checked after every event.
- AddressSanitizer and UndefinedBehaviorSanitizer run on the core suite before the final additional sell-side FOK case. That added case passed the normal final suite. Leak detection disabled because the container blocks LeakSanitizer process inspection.
- HTTP tests: assets, order validation, three-level sweep, cancellation, controlled scenarios, exact export/import replay, benchmark and missing-token rejection.
- JavaScript syntax and Python compilation checks.
- Headless Chromium interaction tests: desktop page, IOC submission, row cancellation, FOK scenario, replay step/play to completion, benchmark rendering, mobile width at 390 pixels and no JavaScript exceptions.
- Desktop and mobile screenshots visually inspected. Desktop screenshot included in README. Performance screenshot is an example measured in this Linux environment, not a promise for other hardware.

## Not claimed

No production exchange validation, live trading connection, crash durability, multithreaded engine, Safari-specific testing, physical Mac execution, hosted GitHub Actions results, or controlled-hardware latency guarantees. CMake configuration is supplied; validation here used the shell build. CI can run after the repository is uploaded.
