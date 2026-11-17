// Walk-through: build a small BTC/USDT book, then hit it with orders.
#include <cstdio>
#include <vector>

#include "obe/order_book.hpp"

using namespace obe;

// 1 tick = 0.01 USDT, 1 lot = 0.001 BTC
static void print_book(const OrderBook& b) {
    auto asks = b.depth(Side::Sell, 5);
    auto bids = b.depth(Side::Buy, 5);
    std::printf("        price        size  orders\n");
    for (auto it = asks.rbegin(); it != asks.rend(); ++it)
        std::printf("  ask %9.2f  %8.3f  %4zu\n", it->price / 100.0, it->qty / 1000.0, it->orders);
    std::printf("  ---------------------------------\n");
    for (const auto& l : bids)
        std::printf("  bid %9.2f  %8.3f  %4zu\n", l.price / 100.0, l.qty / 1000.0, l.orders);
    std::printf("\n");
}

static void print_trades(const char* what, const std::vector<Trade>& trades) {
    std::printf("%s -> %zu trade(s)\n", what, trades.size());
    for (const auto& t : trades)
        std::printf("    #%llu filled against #%llu: %.3f BTC @ %.2f\n",
                    static_cast<unsigned long long>(t.taker_id),
                    static_cast<unsigned long long>(t.maker_id), t.qty / 1000.0, t.price / 100.0);
    std::printf("\n");
}

int main() {
    OrderBook b;
    b.submit_limit(1, Side::Sell, 6'500'500, 250);
    b.submit_limit(2, Side::Sell, 6'500'500, 100);
    b.submit_limit(3, Side::Sell, 6'501'000, 500);
    b.submit_limit(4, Side::Buy, 6'499'500, 300);
    b.submit_limit(5, Side::Buy, 6'499'000, 800);
    std::printf("Initial book\n");
    print_book(b);

    print_trades("Buy 0.300 BTC limit 65,006.00",
                 b.submit_limit(6, Side::Buy, 6'500'600, 300));
    print_book(b);

    print_trades("Market sell 0.500 BTC", b.submit_market(7, Side::Sell, 500));
    print_book(b);

    b.cancel(3);
    std::printf("Cancelled #3\n");
    print_book(b);
    return 0;
}
