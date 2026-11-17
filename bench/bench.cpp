// Throughput and latency benchmark on a synthetic but realistic order flow:
// most orders rest near the touch, many are cancelled, some cross the spread.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "obe/order_book.hpp"

using namespace obe;
using Clock = std::chrono::steady_clock;

struct Op {
    enum Kind : std::uint8_t { Limit, Market, Cancel } kind;
    Side side;
    OrderId id;
    Price price;
    Qty qty;
};

static std::vector<Op> make_flow(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> offset(0.0, 8.0);  // ticks away from mid
    std::uniform_int_distribution<int> roll(0, 99), lots(1, 50);
    std::vector<Op> ops;
    std::vector<OrderId> live;
    ops.reserve(n);
    const Price mid = 6'500'000;  // 65,000.00 in 0.01 ticks
    OrderId next = 1;
    while (ops.size() < n) {
        const int r = roll(rng);
        const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
        if (r < 35 && !live.empty()) {  // 35% cancels
            const std::size_t k = rng() % live.size();
            ops.push_back({Op::Cancel, side, live[k], 0, 0});
            live[k] = live.back();
            live.pop_back();
        } else if (r < 40) {  // 5% market orders
            ops.push_back({Op::Market, side, next++, 0, lots(rng)});
        } else {  // 60% limit orders around the mid
            const auto off = static_cast<Price>(offset(rng));
            const Price px = side == Side::Buy ? mid - 1 + off : mid + 1 + off;
            ops.push_back({Op::Limit, side, next, px, lots(rng)});
            live.push_back(next++);
        }
    }
    return ops;
}

int main() {
    constexpr std::size_t kOps = 2'000'000;
    const auto ops = make_flow(kOps, 42);

    // Pass 1: throughput, no timing inside the loop.
    OrderBook book;
    std::size_t trades = 0;
    const auto t0 = Clock::now();
    for (const Op& op : ops) {
        switch (op.kind) {
            case Op::Limit: trades += book.submit_limit(op.id, op.side, op.price, op.qty).size(); break;
            case Op::Market: trades += book.submit_market(op.id, op.side, op.qty).size(); break;
            case Op::Cancel: book.cancel(op.id); break;
        }
    }
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();

    // Pass 2: per-operation latency on a fresh book.
    OrderBook book2;
    std::vector<double> ns;
    ns.reserve(ops.size());
    for (const Op& op : ops) {
        const auto s = Clock::now();
        switch (op.kind) {
            case Op::Limit: (void)book2.submit_limit(op.id, op.side, op.price, op.qty); break;
            case Op::Market: (void)book2.submit_market(op.id, op.side, op.qty); break;
            case Op::Cancel: book2.cancel(op.id); break;
        }
        ns.push_back(std::chrono::duration<double, std::nano>(Clock::now() - s).count());
    }
    std::sort(ns.begin(), ns.end());
    auto pct = [&](double p) { return ns[static_cast<std::size_t>(p * (ns.size() - 1))]; };

    std::printf("operations      : %zu\n", kOps);
    std::printf("trades          : %zu\n", trades);
    std::printf("resting at end  : %zu\n", book.order_count());
    std::printf("throughput      : %.2f million ops/sec\n", kOps / secs / 1e6);
    std::printf("latency p50     : %.0f ns\n", pct(0.50));
    std::printf("latency p99     : %.0f ns\n", pct(0.99));
    std::printf("latency p99.9   : %.0f ns\n", pct(0.999));
    return 0;
}
