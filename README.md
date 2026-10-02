# C-Limit-Order-Book-Matching-Engine
High-performance limit order book and matching engine in C++ with price-time priority, supporting limit/market orders, cancels, and modifies.
# C++ Limit Order Book & Matching Engine

A limit order book (LOB) and matching engine written in modern C++, implementing price-time priority matching as used in electronic exchanges.

## Features

- Limit and market orders
- Order cancellation and modification
- Price-time (FIFO) priority matching
- Partial and full fills with trade generation
- Best bid/offer (BBO) and depth-of-book queries
- Unit tests and a benchmark harness

## Design

| Component | Choice | Why |
|---|---|---|
| Price levels | `std::map` (bids descending, asks ascending) | Ordered access to best price |
| Orders at a level | Doubly linked list (`std::list`) | O(1) insert/cancel, FIFO preserved |
| Order lookup | `std::unordered_map<OrderId, Iterator>` | O(1) cancel/modify by ID |

*(Edit this table to match your actual implementation.)*

## Complexity

| Operation | Complexity |
|---|---|
| Add order | O(log M) |
| Cancel order | O(1) average |
| Match at best price | O(1) per fill |

M = number of distinct price levels.

## Build & Run

```bash
git clone https://github.com/<your-username>/<repo-name>.git
cd <repo-name>
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
./order_book_demo
```

## Example

```cpp
OrderBook book;
book.addOrder({1, Side::Buy,  100.50, 100});
book.addOrder({2, Side::Sell, 100.50,  60});  // matches 60 @ 100.50
book.cancelOrder(1);                          // cancels remaining 40
```

## Testing

```bash
ctest --output-on-failure
```

## Benchmarks

| Metric | Result |
|---|---|
| Orders/sec | _fill in after benchmarking_ |
| Median add latency | _fill in_ |

## Roadmap

- [ ] Replace `std::map` with a flat/array-based price ladder
- [ ] Memory pool for order allocation
- [ ] Stop and IOC/FOK order types
- [ ] Market data replay from historical files

## License

MIT
