#pragma once
#include "engine.hpp"
namespace exchange {
// Deliberately simple, independent vector algorithm for differential testing.
class Reference {
    std::vector<Order> orders_;
    std::unordered_set<Id> used_;
    Id seq_ = 0, trade_ = 0;
public:
    std::vector<Order> orders() const { return orders_; }
    Result submit(const Request& r) {
        Result out; out.id = r.id; out.status = "REJECTED";
        if (!r.id || r.id > MAX_ID || r.quantity <= 0 || r.quantity > MAX_VALUE ||
            (r.side != Side::Buy && r.side != Side::Sell) ||
            (r.type != Type::GTC && r.type != Type::IOC && r.type != Type::FOK && r.type != Type::Market) ||
            (r.type == Type::Market ? r.price != 0 : r.price <= 0 || r.price > MAX_VALUE) || used_.count(r.id))
            return out;
        used_.insert(r.id); ++seq_; out.ok = true;
        auto eligible = [&](const Order& o) {
            return o.side != r.side && (r.type == Type::Market ||
                (r.side == Side::Buy ? o.price <= r.price : o.price >= r.price));
        };
        Value available = 0;
        for (const auto& o : orders_) if (eligible(o)) available += o.remaining;
        if (r.type == Type::FOK && available < r.quantity) {
            out.status = "CANCELLED"; out.cancelled = r.quantity; return out;
        }
        Value left = r.quantity;
        while (left > 0) {
            std::size_t selected = orders_.size();
            for (std::size_t i=0; i<orders_.size(); ++i) {
                const auto& o = orders_[i];
                if (!eligible(o)) continue;
                if (selected == orders_.size()) { selected = i; continue; }
                const auto& best = orders_[selected];
                if ((r.side == Side::Buy ? o.price < best.price : o.price > best.price) ||
                    (o.price == best.price && o.sequence < best.sequence)) selected = i;
            }
            if (selected == orders_.size()) break;
            auto& maker = orders_[selected];
            Value quantity = std::min(left,maker.remaining);
            out.trades.push_back({++trade_, r.side == Side::Buy ? r.id : maker.id,
                r.side == Side::Sell ? r.id : maker.id, maker.id,r.id,maker.price,quantity});
            out.filled += quantity; left -= quantity; maker.remaining -= quantity;
            if (!maker.remaining) orders_.erase(orders_.begin() + static_cast<std::ptrdiff_t>(selected));
        }
        if (left && r.type == Type::GTC) {
            orders_.push_back({r.id,r.side,r.price,left,seq_}); out.resting = left;
            out.status = out.filled ? "PARTIAL_RESTING" : "RESTING";
        } else if (left) {
            out.cancelled = left; out.status = out.filled ? "PARTIAL_CANCELLED" : "CANCELLED";
        } else out.status = "FILLED";
        return out;
    }
    Result cancel(Id id) {
        Result out; out.id=id; out.status="REJECTED";
        for (auto it=orders_.begin();it!=orders_.end();++it) if (it->id==id) {
            out.ok=true;out.status="CANCELLED";out.cancelled=it->remaining;orders_.erase(it);break;
        }
        return out;
    }
};
}
