# Architecture and trade-offs

## Data path

Browser POST -> Python serialized command -> C++ stdin -> Engine -> JSON stdout -> browser redraw.
The server holds one persistent C++ subprocess. A re-entrant lock serializes changes; HTTP threads do not concurrently modify the engine. Benchmarking uses a different process and cannot alter the active book.

## Storage

Bids use `map<price, Level, greater<price>>`; asks use `map<price, Level>`. A level holds an aggregate quantity and `list<Order>` in arrival order. The order-ID hash index contains side, price and a list iterator.

Why a list? Erasing one order does not invalidate the iterators for other orders. The index can point directly to a queue node. A vector erase would shift other elements and invalidate such positions. Lists allocate nodes and have weaker cache locality; this is a correctness-friendly design, not proof of minimum latency.

Cancellation does an average O(1) hash lookup, O(log P) price-map lookup, then constant-time queue removal. Last-order removal also removes its price level. P is the number of price levels. ID hashing has worst-case linear behaviour; do not advertise unconditional O(1) cancellation.

Best price access is O(1) at map begin. Adding a resting order is O(log P), plus average constant-time index insertion and queue append. FOK precheck scans eligible levels O(P). Matching touches executed orders and exhausted levels; there is no full-book scan for every fill. UI snapshot generation still scans stored orders and levels before output truncation, so UI response time does not equal core matching latency.

## Matching transaction

1. Validate fields and duplicate/capacity constraints before logical changes.
2. Record accepted ID and assign next arrival sequence.
3. FOK only: sum eligible aggregate liquidity by subtracting from needed quantity. Insufficient quantity returns cancellation before trades.
4. Select best opposite level and oldest order.
5. If price crosses (or market instruction), execute minimum remaining quantity at maker price.
6. Update both quantities, level aggregate and trade records.
7. Remove exhausted maker and index record; remove an empty level.
8. Repeat while incoming quantity remains and eligible liquidity exists.
9. Store GTC remainder; cancel all other remainder.

An accepted FOK cancellation consumes its ID; invalid submissions do not. Arrival priority comes from engine sequence, never client-supplied ID. Partially filled resting orders keep their position.

## Invariants

Every active quantity is positive. Level totals equal summed queue quantities. Queue sequences increase. Index records resolve to exactly the corresponding node. Every active ID is a previously accepted ID. Total indexed count equals queue count. After matching, best bid is below best ask when both exist.

`validate()` explicitly checks these in tests. The random tests compare result status, trade IDs/prices/quantities, and all remaining orders against an independent vector algorithm. Quantity conservation is checked for every accepted randomized request.

## Interfaces and failure boundaries

C++ program writes one JSON document per command. Stdout is reserved for protocol; errors go to stderr. Python does not compute or modify execution quantities. Numeric parser does not depend on `from_chars`, and rejects signs, mixed text and overflow.

The browser never invents market prices, fills or throughput. Price is always ticks. Full command journals are exported as plain text. Engine history returned to the UI is bounded, but CLI command responses include all trades for that event.

Allocation failures are fatal and not rolled back. The local bridge has no automatic engine restart or journal recovery. Disk durability and multi-user concurrency are out of scope. Browser mutations are protected with host/origin checks and a session token; binding is loopback only.
