# orderbook-engine

[![CI](https://github.com/Do0vm/orderbook-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/Do0vm/orderbook-engine/actions/workflows/ci.yml)

A **limit order book matching engine** in C++20: the piece of software at the centre of every crypto exchange, which decides who trades with whom and at what price.

- **Price-time priority:** best price first; at the same price, the earliest order first
- **Order types:** limit (good-till-cancel), immediate-or-cancel, market; cancel and reduce-in-place
- **O(1) cancels** via an order-id index into stable list iterators; O(log n) price-level lookup
- **Integer prices and sizes** (ticks and lots), because exchanges never match on floating point
- **About 10 million operations per second** with a median latency of about 100 ns on a GitHub Actions runner
- **Tested against a reference model** on 200,000 random operations, and run under AddressSanitizer + UndefinedBehaviorSanitizer in CI

## Demo

`./build/obe_demo` builds a small BTC/USDT book and hits it with orders (output from CI):

```
Initial book
  ask  65010.00     0.500     1
  ask  65005.00     0.350     2
  ---------------------------------
  bid  64995.00     0.300     1
  bid  64990.00     0.800     1

Buy 0.300 BTC limit 65,006.00 -> 2 trade(s)
    #6 filled against #1: 0.250 BTC @ 65005.00
    #6 filled against #2: 0.050 BTC @ 65005.00

Market sell 0.500 BTC -> 2 trade(s)
    #7 filled against #4: 0.300 BTC @ 64995.00
    #7 filled against #5: 0.200 BTC @ 64990.00
```

The buyer was willing to pay 65,006 but filled at 65,005, because trades happen at the **resting (maker) order's price**. Order #1 filled before #2 at the same price because it arrived first.

## Design

```
bids_: std::map<Price, PriceLevel, greater>   best bid = begin()
asks_: std::map<Price, PriceLevel, less>      best ask = begin()
PriceLevel = { std::list<Order> queue, Qty total }   front = oldest
index_: unordered_map<OrderId, {side, price, list::iterator}>
```

| Operation | Cost | Why |
|---|---|---|
| New order (no fill) | O(log L) | find or insert the price level (L = number of levels) |
| Match | O(fills + levels crossed) | always take `begin()` of the opposite map, then the front of its queue |
| Cancel | O(1) average | the index stores the order's list iterator; `std::list` iterators stay valid when other orders come and go |
| Best bid/ask | O(1) | `begin()` of each map |

`reduce()` shrinks an order **without** losing its queue position (it can't hurt anyone behind it). Increasing size is rejected, because real venues treat that as cancel + new order at the back of the queue.

## Testing

`tests/test_order_book.cpp` has scenario tests (crossing, partial fills, sweeping several levels, IOC, market orders on an empty book, cancels, priority after reduce, bad input) plus a **differential test**:

`tests/reference_book.hpp` is a deliberately naive book that scans every order on every match. It's far too slow for production but easy to verify by reading. 200,000 random limit, IOC, market and cancel operations go to both books, and the test checks that every trade, the best bid/ask and the order count match after each one, and that the book is never left crossed.

CI builds and tests on **GCC, Clang and MSVC**, and runs the tests again under **ASan + UBSan** to catch memory errors and undefined behaviour.

## Benchmark

`bench/bench.cpp` replays 2,000,000 synthetic operations: 60% limit orders clustered near the mid-price, 35% cancels, 5% market orders. Results from GitHub Actions (ubuntu-latest, Release build):

| | GCC / Clang runs |
|---|---|
| Throughput | 8.5 – 10.3 million ops/sec |
| Latency p50 | 90 – 120 ns |
| Latency p99 | 340 – 620 ns |
| Latency p99.9 | about 1.5 µs |
| Trades generated | 1,004,813 |

Shared CI machines are noisy, so treat these as rough figures. Per-operation latency includes two `steady_clock` reads.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/obe_bench
./build/obe_demo
```

## What a production engine would add

- A memory pool or intrusive lists instead of `std::list` (fewer allocations, better cache locality)
- Flat arrays indexed by tick for the price levels near the touch
- Self-trade prevention, post-only and stop orders
- A sequenced event log (journal) so the book can be rebuilt after a crash, and market-data output of the book changes
