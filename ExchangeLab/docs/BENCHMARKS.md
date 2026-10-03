# Benchmark methodology

Run through the Performance tab for machine metadata and downloadable JSON, or `./build/benchmark` for raw core results. No results are hardcoded into the dashboard.

- Synthetic deterministic PRNG seed: 2026.
- 16,000 events: 4,000 initial resting orders, then 12,000 placements/cancellations/IOC orders.
- 200 possible price levels (100 each side) in initial population.
- One complete untimed warm-up per model, then three measured repetitions on fresh models.
- Optimized price-level engine first, vector reference second. Order is not randomized; this can introduce cache/thermal/order bias.
- `steady_clock` measures each event; reported p50/p95/p99 pooled over 48,000 event samples per model.
- Throughput includes timer calls, vector sample recording and simple result accounting; per-event latency stops after API result construction.
- Excludes JSON, Python, HTTP and browser rendering. Includes dynamic allocation and engine bookkeeping.
- Checksum prevents unused result elimination and provides a coarse sanity check. Full result equality is verified separately by differential tests, not this checksum.
- Both models retain used IDs, but optimized model also keeps depth aggregates, active-ID index and bounded recent history. The reference deliberately scans a vector and has fewer bookkeeping tasks. Results are not an isolated apples-to-apples container microbenchmark.
- Built by shell script with `-std=c++17 -O2`. No CPU pinning, controlled frequency, memory pool, allocator tuning or kernel bypass.

Report hardware model, OS, compiler/version, build flags, seed, workload, repeated runs and percentiles. Run with other work quiet and repeat to inspect variability. Numbers from this synthetic workload cannot establish live exchange latency or a production SLA.

Suggested next benchmark work: isolated add/cancel/match/FOK paths, varied queue/price-level sizes, larger sweeps, alternating engine execution order, independent repetitions with confidence intervals, profiler traces and allocation counts.
