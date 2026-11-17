#include "obe/order_book.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace obe {

void OrderBook::validate_new(OrderId id, Qty qty) const {
    if (qty <= 0) throw std::invalid_argument("quantity must be positive");
    if (index_.count(id) != 0) throw std::invalid_argument("duplicate order id");
}

template <class Book, class Crosses>
void OrderBook::match(Book& book, OrderId taker, Side taker_side, Qty& remaining,
                      Crosses crosses, std::vector<Trade>& out) {
    while (remaining > 0 && !book.empty()) {
        auto level_it = book.begin();  // best price on the opposite side
        if (!crosses(level_it->first)) break;
        PriceLevel& level = level_it->second;

        while (remaining > 0 && !level.queue.empty()) {
            Order& maker = level.queue.front();  // oldest order at this price
            const Qty fill = std::min(remaining, maker.qty);
            out.push_back(Trade{maker.id, taker, taker_side, maker.price, fill});
            maker.qty -= fill;
            level.total -= fill;
            remaining -= fill;
            if (maker.qty == 0) {
                index_.erase(maker.id);
                level.queue.pop_front();
            }
        }
        if (level.queue.empty()) book.erase(level_it);
    }
}

template <class Book>
void OrderBook::rest(Book& book, OrderId id, Side side, Price price, Qty qty) {
    PriceLevel& level = book[price];
    level.queue.push_back(Order{id, side, price, qty});
    level.total += qty;
    index_.emplace(id, Location{side, price, std::prev(level.queue.end())});
}

template <class Book>
void OrderBook::erase(Book& book, const Location& loc) {
    auto level_it = book.find(loc.price);
    level_it->second.total -= loc.it->qty;
    level_it->second.queue.erase(loc.it);
    if (level_it->second.queue.empty()) book.erase(level_it);
}

std::vector<Trade> OrderBook::submit_limit(OrderId id, Side side, Price price, Qty qty,
                                           TimeInForce tif) {
    validate_new(id, qty);
    std::vector<Trade> trades;
    Qty remaining = qty;
    if (side == Side::Buy) {
        match(asks_, id, side, remaining, [price](Price ask) { return ask <= price; }, trades);
        if (remaining > 0 && tif == TimeInForce::GTC) rest(bids_, id, side, price, remaining);
    } else {
        match(bids_, id, side, remaining, [price](Price bid) { return bid >= price; }, trades);
        if (remaining > 0 && tif == TimeInForce::GTC) rest(asks_, id, side, price, remaining);
    }
    return trades;
}

std::vector<Trade> OrderBook::submit_market(OrderId id, Side side, Qty qty) {
    validate_new(id, qty);
    std::vector<Trade> trades;
    Qty remaining = qty;
    auto any_price = [](Price) { return true; };
    if (side == Side::Buy) {
        match(asks_, id, side, remaining, any_price, trades);
    } else {
        match(bids_, id, side, remaining, any_price, trades);
    }
    return trades;
}

bool OrderBook::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;
    const Location loc = it->second;
    if (loc.side == Side::Buy) {
        erase(bids_, loc);
    } else {
        erase(asks_, loc);
    }
    index_.erase(it);
    return true;
}

bool OrderBook::reduce(OrderId id, Qty new_qty) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;
    if (new_qty <= 0) return cancel(id);
    const Location& loc = it->second;
    const Qty delta = loc.it->qty - new_qty;
    if (delta <= 0) return false;  // increase (or no-op) not allowed
    loc.it->qty = new_qty;
    if (loc.side == Side::Buy) {
        bids_.find(loc.price)->second.total -= delta;
    } else {
        asks_.find(loc.price)->second.total -= delta;
    }
    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Qty OrderBook::volume_at(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it == bids_.end() ? 0 : it->second.total;
    }
    auto it = asks_.find(price);
    return it == asks_.end() ? 0 : it->second.total;
}

std::vector<Level> OrderBook::depth(Side side, std::size_t levels) const {
    std::vector<Level> out;
    auto collect = [&](const auto& book) {
        for (const auto& [price, level] : book) {
            if (out.size() == levels) break;
            out.push_back(Level{price, level.total, level.queue.size()});
        }
    };
    if (side == Side::Buy) {
        collect(bids_);
    } else {
        collect(asks_);
    }
    return out;
}

}  // namespace obe
