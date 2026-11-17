// A deliberately simple, obviously-correct order book used only in tests.
// It scans every resting order on every match (O(n)), which is far too slow
// for production but easy to verify by reading. The real engine must produce
// exactly the same trades for any sequence of orders.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "obe/order_book.hpp"

namespace ref {

using namespace obe;

class ReferenceBook {
public:
    std::vector<Trade> submit(OrderId id, Side side, std::optional<Price> limit, Qty qty,
                              bool rest_remainder) {
        std::vector<Trade> out;
        while (qty > 0) {
            int best = -1;
            for (int i = 0; i < static_cast<int>(orders_.size()); ++i) {
                const Resting& o = orders_[i];
                if (o.side == side) continue;
                const bool crosses = !limit || (side == Side::Buy ? o.price <= *limit
                                                                  : o.price >= *limit);
                if (!crosses) continue;
                if (best < 0 || better(o, orders_[best], side)) best = i;
            }
            if (best < 0) break;
            Resting& m = orders_[best];
            const Qty fill = std::min(qty, m.qty);
            out.push_back(Trade{m.id, id, side, m.price, fill});
            m.qty -= fill;
            qty -= fill;
            if (m.qty == 0) orders_.erase(orders_.begin() + best);
        }
        if (qty > 0 && rest_remainder && limit) {
            orders_.push_back(Resting{id, side, *limit, qty, seq_++});
        }
        return out;
    }

    bool cancel(OrderId id) {
        auto it = std::find_if(orders_.begin(), orders_.end(),
                               [id](const Resting& o) { return o.id == id; });
        if (it == orders_.end()) return false;
        orders_.erase(it);
        return true;
    }

    std::optional<Price> best(Side side) const {
        std::optional<Price> p;
        for (const Resting& o : orders_) {
            if (o.side != side) continue;
            if (!p || (side == Side::Buy ? o.price > *p : o.price < *p)) p = o.price;
        }
        return p;
    }

    std::size_t size() const { return orders_.size(); }

private:
    struct Resting {
        OrderId id;
        Side side;
        Price price;
        Qty qty;
        std::uint64_t seq;
    };

    // Is `a` matched before `b` by an incoming order on `taker_side`?
    static bool better(const Resting& a, const Resting& b, Side taker_side) {
        if (a.price != b.price) {
            return taker_side == Side::Buy ? a.price < b.price : a.price > b.price;
        }
        return a.seq < b.seq;  // same price: earlier order first
    }

    std::vector<Resting> orders_;
    std::uint64_t seq_ = 0;
};

}  // namespace ref
