#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>

#include "obe/order_book.hpp"
#include "reference_book.hpp"

using namespace obe;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                         #cond);                                                 \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_THROWS(expr)                         \
    do {                                           \
        bool threw = false;                        \
        try {                                      \
            (void)(expr);                          \
        } catch (const std::invalid_argument&) {   \
            threw = true;                          \
        }                                          \
        CHECK(threw && "expected invalid_argument"); \
    } while (0)

static void resting_orders_do_not_trade() {
    OrderBook b;
    CHECK(b.submit_limit(1, Side::Buy, 99, 5).empty());
    CHECK(b.submit_limit(2, Side::Sell, 101, 7).empty());
    CHECK(b.best_bid() == 99);
    CHECK(b.best_ask() == 101);
    CHECK(b.volume_at(Side::Buy, 99) == 5);
    CHECK(b.order_count() == 2);
}

static void crossing_order_trades_at_maker_price() {
    OrderBook b;
    b.submit_limit(1, Side::Sell, 100, 5);
    auto t = b.submit_limit(2, Side::Buy, 105, 5);  // willing to pay more
    CHECK(t.size() == 1);
    CHECK(t[0] == (Trade{1, 2, Side::Buy, 100, 5}));  // gets the better price
    CHECK(b.order_count() == 0);
    CHECK(!b.best_ask().has_value());
}

static void same_price_fills_in_time_order() {
    OrderBook b;
    b.submit_limit(1, Side::Sell, 100, 3);
    b.submit_limit(2, Side::Sell, 100, 3);
    b.submit_limit(3, Side::Sell, 100, 3);
    auto t = b.submit_limit(4, Side::Buy, 100, 5);
    CHECK(t.size() == 2);
    CHECK(t[0].maker_id == 1 && t[0].qty == 3);
    CHECK(t[1].maker_id == 2 && t[1].qty == 2);
    CHECK(b.volume_at(Side::Sell, 100) == 4);  // 1 left on #2, 3 on #3
}

static void partial_fill_rests_remainder() {
    OrderBook b;
    b.submit_limit(1, Side::Sell, 100, 2);
    auto t = b.submit_limit(2, Side::Buy, 100, 5);
    CHECK(t.size() == 1 && t[0].qty == 2);
    CHECK(b.best_bid() == 100);
    CHECK(b.volume_at(Side::Buy, 100) == 3);
    CHECK(b.contains(2));
}

static void sweeps_multiple_levels_best_first() {
    OrderBook b;
    b.submit_limit(1, Side::Sell, 102, 1);
    b.submit_limit(2, Side::Sell, 100, 1);
    b.submit_limit(3, Side::Sell, 101, 1);
    b.submit_limit(4, Side::Sell, 103, 1);
    auto t = b.submit_limit(5, Side::Buy, 102, 10);
    CHECK(t.size() == 3);
    CHECK(t[0].price == 100 && t[1].price == 101 && t[2].price == 102);
    CHECK(b.best_ask() == 103);   // 103 was above the limit
    CHECK(b.best_bid() == 102);   // 7 lots left over rest at the limit
    CHECK(b.volume_at(Side::Buy, 102) == 7);
}

static void ioc_never_rests() {
    OrderBook b;
    b.submit_limit(1, Side::Buy, 100, 2);
    auto t = b.submit_limit(2, Side::Sell, 100, 5, TimeInForce::IOC);
    CHECK(t.size() == 1 && t[0].qty == 2);
    CHECK(!b.contains(2));
    CHECK(!b.best_ask().has_value());
}

static void market_order_on_empty_book() {
    OrderBook b;
    CHECK(b.submit_market(1, Side::Buy, 10).empty());
    CHECK(b.order_count() == 0);
}

static void market_order_walks_book() {
    OrderBook b;
    b.submit_limit(1, Side::Buy, 100, 2);
    b.submit_limit(2, Side::Buy, 98, 2);
    auto t = b.submit_market(3, Side::Sell, 3);
    CHECK(t.size() == 2);
    CHECK(t[0].price == 100 && t[0].qty == 2);
    CHECK(t[1].price == 98 && t[1].qty == 1);
    CHECK(b.volume_at(Side::Buy, 98) == 1);
}

static void cancel_removes_order_and_empty_level() {
    OrderBook b;
    b.submit_limit(1, Side::Buy, 100, 5);
    b.submit_limit(2, Side::Buy, 99, 5);
    CHECK(b.cancel(1));
    CHECK(!b.cancel(1));   // already gone
    CHECK(!b.cancel(42));  // never existed
    CHECK(b.best_bid() == 99);
    CHECK(b.volume_at(Side::Buy, 100) == 0);
}

static void reduce_keeps_queue_priority() {
    OrderBook b;
    b.submit_limit(1, Side::Sell, 100, 10);
    b.submit_limit(2, Side::Sell, 100, 10);
    CHECK(b.reduce(1, 4));
    CHECK(!b.reduce(1, 8));  // increases are rejected
    CHECK(b.volume_at(Side::Sell, 100) == 14);
    auto t = b.submit_limit(3, Side::Buy, 100, 5);
    CHECK(t[0].maker_id == 1 && t[0].qty == 4);  // still first in line
    CHECK(t[1].maker_id == 2 && t[1].qty == 1);
}

static void depth_snapshot() {
    OrderBook b;
    b.submit_limit(1, Side::Buy, 100, 1);
    b.submit_limit(2, Side::Buy, 100, 2);
    b.submit_limit(3, Side::Buy, 99, 4);
    b.submit_limit(4, Side::Buy, 98, 8);
    auto d = b.depth(Side::Buy, 2);
    CHECK(d.size() == 2);
    CHECK(d[0].price == 100 && d[0].qty == 3 && d[0].orders == 2);
    CHECK(d[1].price == 99 && d[1].qty == 4 && d[1].orders == 1);
}

static void rejects_bad_input() {
    OrderBook b;
    b.submit_limit(1, Side::Buy, 100, 1);
    CHECK_THROWS(b.submit_limit(1, Side::Sell, 101, 1));  // duplicate id
    CHECK_THROWS(b.submit_limit(2, Side::Sell, 101, 0));
    CHECK_THROWS(b.submit_market(3, Side::Sell, -1));
}

// Differential test: 200,000 random operations against the reference model.
static void matches_reference_model_on_random_flow() {
    OrderBook fast;
    ref::ReferenceBook slow;
    std::mt19937_64 rng(20261117);
    std::uniform_int_distribution<int> op(0, 99), px(95, 105), qty(1, 10);
    std::vector<OrderId> ids;
    OrderId next = 1;
    int mismatches = 0;

    for (int i = 0; i < 200000 && mismatches == 0; ++i) {
        const int o = op(rng);
        const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
        if (o < 20 && !ids.empty()) {
            const OrderId id = ids[rng() % ids.size()];
            if (fast.cancel(id) != slow.cancel(id)) ++mismatches;
        } else if (o < 30) {
            const Qty q = qty(rng);
            if (fast.submit_market(next, side, q) != slow.submit(next, side, std::nullopt, q, false))
                ++mismatches;
            ++next;
        } else {
            const Price p = px(rng);
            const Qty q = qty(rng);
            const bool ioc = o < 40;
            auto a = fast.submit_limit(next, side, p, q, ioc ? TimeInForce::IOC : TimeInForce::GTC);
            auto b = slow.submit(next, side, p, q, !ioc);
            if (a != b) ++mismatches;
            ids.push_back(next++);
        }
        if (fast.best_bid() != slow.best(Side::Buy) || fast.best_ask() != slow.best(Side::Sell) ||
            fast.order_count() != slow.size()) {
            ++mismatches;
        }
        // The book must never be left crossed.
        if (fast.best_bid() && fast.best_ask() && *fast.best_bid() >= *fast.best_ask()) ++mismatches;
    }
    CHECK(mismatches == 0);
}

int main() {
    resting_orders_do_not_trade();
    crossing_order_trades_at_maker_price();
    same_price_fills_in_time_order();
    partial_fill_rests_remainder();
    sweeps_multiple_levels_best_first();
    ioc_never_rests();
    market_order_on_empty_book();
    market_order_walks_book();
    cancel_removes_order_and_empty_level();
    reduce_keeps_queue_priority();
    depth_snapshot();
    rejects_bad_input();
    matches_reference_model_on_random_flow();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
