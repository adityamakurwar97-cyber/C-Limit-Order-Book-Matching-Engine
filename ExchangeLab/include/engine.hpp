#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace exchange {
using Id = std::uint64_t;
using Value = std::int64_t;
constexpr Id MAX_ID = 9007199254740991ULL; // Exact in browser JSON numbers.
constexpr Value MAX_VALUE = 1000000000LL;
enum class Side { Buy, Sell };
enum class Type { GTC, IOC, FOK, Market };
inline const char* name(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }
inline const char* name(Type t) {
    switch(t) { case Type::GTC: return "GTC"; case Type::IOC: return "IOC";
        case Type::FOK: return "FOK"; case Type::Market: return "MARKET"; }
    return "INVALID";
}
struct Request { Id id; Side side; Type type; Value price; Value quantity; };
struct Order { Id id; Side side; Value price; Value remaining; Id sequence; };
struct Trade { Id sequence; Id buy; Id sell; Id maker; Id taker; Value price; Value quantity; };
struct Result {
    bool ok = false;
    std::string status;
    std::string message;
    Id id = 0;
    Value filled = 0;
    Value resting = 0;
    Value cancelled = 0;
    std::vector<Trade> trades;
};
struct Level { Value total = 0; std::list<Order> queue; };
struct Depth { Value price; Value quantity; std::size_t orders; };

class Engine {
    using Bids = std::map<Value, Level, std::greater<Value>>;
    using Asks = std::map<Value, Level>;
    struct Location { Side side; Value price; std::list<Order>::iterator order; };
    Bids bids_;
    Asks asks_;
    std::unordered_map<Id, Location> index_;
    std::unordered_set<Id> used_;
    Id sequence_ = 0, trade_sequence_ = 0;
    std::deque<Trade> recent_;
    Value volume_ = 0;

    static bool crosses(const Request& r, Value price) {
        return r.type == Type::Market ||
            (r.side == Side::Buy ? r.price >= price : r.price <= price);
    }
    template<class Map> bool fillable(const Request& r, const Map& levels) const {
        Value need = r.quantity;
        for (const auto& entry : levels) {
            if (!crosses(r, entry.first)) break;
            if (entry.second.total >= need) return true;
            need -= entry.second.total; // No accumulation overflow.
        }
        return false;
    }
    template<class Map> void match(const Request& r, Value& remaining, Map& levels, Result& out) {
        while (remaining > 0 && !levels.empty()) {
            auto level = levels.begin();
            if (!crosses(r, level->first)) break;
            auto& queue = level->second.queue;
            auto& maker = queue.front();
            const Value quantity = std::min(remaining, maker.remaining);
            Trade trade{++trade_sequence_, r.side == Side::Buy ? r.id : maker.id,
                        r.side == Side::Sell ? r.id : maker.id,
                        maker.id, r.id, maker.price, quantity};
            out.trades.push_back(trade);
            recent_.push_back(trade);
            if (recent_.size() > 200) recent_.pop_front();
            volume_ += quantity;
            out.filled += quantity;
            remaining -= quantity;
            maker.remaining -= quantity;
            level->second.total -= quantity;
            if (maker.remaining == 0) {
                index_.erase(maker.id);
                queue.pop_front();
            }
            if (queue.empty()) levels.erase(level);
        }
    }
    template<class Map> void rest(const Request& r, Value remaining, Map& levels) {
        auto& level = levels[r.price];
        level.queue.push_back({r.id, r.side, r.price, remaining, sequence_});
        level.total += remaining;
        index_.emplace(r.id, Location{r.side, r.price, std::prev(level.queue.end())});
    }
    template<class Map> void remove(const Location& location, Map& levels) {
        auto level = levels.find(location.price);
        level->second.total -= location.order->remaining;
        level->second.queue.erase(location.order);
        if (level->second.queue.empty()) levels.erase(level);
    }
    template<class Map> void validateSide(const Map& levels, Side side, std::size_t& count) const {
        for (const auto& entry : levels) {
            Value total = 0;
            Id previous = 0;
            if (entry.second.queue.empty()) throw std::logic_error("Empty level");
            for (const auto& o : entry.second.queue) {
                if (o.side != side || o.price != entry.first || o.remaining <= 0 || o.sequence <= previous)
                    throw std::logic_error("Order/FIFO invariant");
                auto found = index_.find(o.id);
                if (found == index_.end() || found->second.side != side || found->second.price != o.price ||
                    &*found->second.order != &o || !used_.count(o.id))
                    throw std::logic_error("Index invariant");
                previous = o.sequence;
                total += o.remaining;
                ++count;
            }
            if (total != entry.second.total) throw std::logic_error("Level total invariant");
        }
    }
public:
    Engine() = default;
    Engine(const Engine&) = delete; // Index contains iterators into this instance.
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    Result submit(const Request& r) {
        Result out;
        out.id = r.id;
        out.status = "REJECTED";
        if (r.id == 0 || r.id > MAX_ID || r.quantity <= 0 || r.quantity > MAX_VALUE ||
            (r.side != Side::Buy && r.side != Side::Sell) ||
            (r.type != Type::GTC && r.type != Type::IOC && r.type != Type::FOK && r.type != Type::Market)) {
            out.message = "Invalid ID, side, type or quantity"; return out;
        }
        if ((r.type == Type::Market && r.price != 0) ||
            (r.type != Type::Market && (r.price <= 0 || r.price > MAX_VALUE))) {
            out.message = "Limit price must be 1..1e9; MARKET price must be 0"; return out;
        }
        if (used_.count(r.id)) { out.message = "ID already used in this session"; return out; }
        // Bounded session sizes keep aggregates within int64 and prevent unbounded memory use.
        if (used_.size() >= 1000000 || index_.size() >= 100000) {
            out.message = "Session capacity reached; start a new session"; return out;
        }
        used_.insert(r.id);
        ++sequence_;
        out.ok = true;
        if (r.type == Type::FOK && !(r.side == Side::Buy ? fillable(r, asks_) : fillable(r, bids_))) {
            out.status = "CANCELLED"; out.cancelled = r.quantity;
            out.message = "FOK: insufficient eligible liquidity; no trades";
            return out;
        }
        Value remaining = r.quantity;
        if (r.side == Side::Buy) match(r, remaining, asks_, out);
        else match(r, remaining, bids_, out);
        if (remaining > 0 && r.type == Type::GTC) {
            if (r.side == Side::Buy) rest(r, remaining, bids_);
            else rest(r, remaining, asks_);
            out.resting = remaining;
            out.status = out.filled ? "PARTIAL_RESTING" : "RESTING";
        } else if (remaining > 0) {
            out.cancelled = remaining;
            out.status = out.filled ? "PARTIAL_CANCELLED" : "CANCELLED";
        } else out.status = "FILLED";
        return out;
    }
    Result cancel(Id id) {
        Result out; out.id = id;
        const auto found = index_.find(id);
        if (found == index_.end()) {
            out.status = "REJECTED"; out.message = "No active order with that ID"; return out;
        }
        out.cancelled = found->second.order->remaining;
        if (found->second.side == Side::Buy) remove(found->second, bids_);
        else remove(found->second, asks_);
        index_.erase(found);
        out.ok = true; out.status = "CANCELLED";
        return out;
    }
    void clear() {
        index_.clear(); bids_.clear(); asks_.clear(); used_.clear(); recent_.clear();
        sequence_ = 0; trade_sequence_ = 0; volume_ = 0;
    }
    std::vector<Order> orders() const {
        std::vector<Order> out;
        for (const auto& level : bids_) for (const auto& order : level.second.queue) out.push_back(order);
        for (const auto& level : asks_) for (const auto& order : level.second.queue) out.push_back(order);
        return out;
    }
    std::vector<Depth> depth(Side side) const {
        std::vector<Depth> out;
        if (side == Side::Buy) for (const auto& e : bids_) out.push_back({e.first, e.second.total, e.second.queue.size()});
        else for (const auto& e : asks_) out.push_back({e.first, e.second.total, e.second.queue.size()});
        return out;
    }
    const std::deque<Trade>& recent() const { return recent_; }
    Id tradeCount() const { return trade_sequence_; }
    Value volume() const { return volume_; }
    std::size_t activeCount() const { return index_.size(); }
    void validate() const {
        std::size_t count = 0;
        validateSide(bids_, Side::Buy, count); validateSide(asks_, Side::Sell, count);
        if (count != index_.size()) throw std::logic_error("Index count invariant");
        if (!bids_.empty() && !asks_.empty() && bids_.begin()->first >= asks_.begin()->first)
            throw std::logic_error("Crossed resting book");
    }
};
} // namespace exchange
