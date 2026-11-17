// Limit order book with price-time priority matching.
//
// Prices and quantities are integers (ticks and lots). Exchanges never match
// on floating-point prices, because 0.1 + 0.2 != 0.3.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace obe {

using OrderId = std::uint64_t;
using Price = std::int64_t;  // in ticks, e.g. 1 tick = 0.01 USDT
using Qty = std::int64_t;    // in lots,  e.g. 1 lot  = 0.0001 BTC

enum class Side : std::uint8_t { Buy, Sell };

enum class TimeInForce : std::uint8_t {
    GTC,  // good-till-cancel: any unfilled remainder rests on the book
    IOC,  // immediate-or-cancel: any unfilled remainder is discarded
};

struct Trade {
    OrderId maker_id;  // resting order that provided liquidity
    OrderId taker_id;  // incoming order that removed it
    Side taker_side;
    Price price;  // always the maker's price
    Qty qty;

    bool operator==(const Trade&) const = default;
};

struct Level {
    Price price;
    Qty qty;
    std::size_t orders;
};

class OrderBook {
public:
    // Match against the opposite side, then rest (GTC) or drop (IOC) the
    // remainder. Throws std::invalid_argument on qty <= 0 or a duplicate id.
    std::vector<Trade> submit_limit(OrderId id, Side side, Price price, Qty qty,
                                    TimeInForce tif = TimeInForce::GTC);

    // Take liquidity at any price until filled or the other side is empty.
    // Never rests.
    std::vector<Trade> submit_market(OrderId id, Side side, Qty qty);

    // Remove a resting order. Returns false if the id is not on the book.
    bool cancel(OrderId id);

    // Shrink a resting order in place. Keeps its time priority (reducing
    // can't disadvantage anyone queued behind it). Increasing is not allowed:
    // real venues treat that as cancel + new order at the back of the queue.
    bool reduce(OrderId id, Qty new_qty);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Qty volume_at(Side side, Price price) const;
    std::vector<Level> depth(Side side, std::size_t levels) const;
    std::size_t order_count() const { return index_.size(); }
    bool contains(OrderId id) const { return index_.count(id) != 0; }

private:
    struct Order {
        OrderId id;
        Side side;
        Price price;
        Qty qty;
    };
    using Queue = std::list<Order>;  // stable iterators -> O(1) cancel
    struct PriceLevel {
        Queue queue;  // front = oldest = first to fill
        Qty total = 0;
    };
    // Best price first on both sides.
    using Bids = std::map<Price, PriceLevel, std::greater<Price>>;
    using Asks = std::map<Price, PriceLevel, std::less<Price>>;

    struct Location {
        Side side;
        Price price;
        Queue::iterator it;
    };

    template <class Book, class Crosses>
    void match(Book& book, OrderId taker, Side taker_side, Qty& remaining,
               Crosses crosses, std::vector<Trade>& out);

    template <class Book>
    void rest(Book& book, OrderId id, Side side, Price price, Qty qty);

    template <class Book>
    void erase(Book& book, const Location& loc);

    void validate_new(OrderId id, Qty qty) const;

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Location> index_;
};

}  // namespace obe
