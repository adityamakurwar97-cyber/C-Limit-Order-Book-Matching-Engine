#pragma once
#include "engine.hpp"
#include <limits>
#include <sstream>
namespace exchange {
template<class T> T integer(const std::string& token) {
    if (token.empty()) throw std::invalid_argument("Missing integer");
    T value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') throw std::invalid_argument("Digits only: " + token);
        const T digit = static_cast<T>(c - '0');
        if (value > (std::numeric_limits<T>::max() - digit) / 10)
            throw std::invalid_argument("Integer overflow");
        value = static_cast<T>(value * 10 + digit);
    }
    return value;
}
inline std::vector<std::string> tokens(const std::string& line) {
    std::istringstream input(line); std::vector<std::string> words;
    for (std::string w; input >> w;) words.push_back(w);
    return words;
}
inline Request request(const std::vector<std::string>& w) {
    if (w.size() != 5 || (w[0] != "BUY" && w[0] != "SELL"))
        throw std::invalid_argument("Use BUY|SELL id price quantity GTC|IOC|FOK|MARKET");
    Type t;
    if (w[4] == "GTC") t = Type::GTC;
    else if (w[4] == "IOC") t = Type::IOC;
    else if (w[4] == "FOK") t = Type::FOK;
    else if (w[4] == "MARKET") t = Type::Market;
    else throw std::invalid_argument("Unknown order type");
    return {integer<Id>(w[1]), w[0] == "BUY" ? Side::Buy : Side::Sell,
            t, integer<Value>(w[2]), integer<Value>(w[3])};
}
inline std::string escaped(const std::string& s) {
    std::ostringstream o; o << '"';
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') o << '\\' << c;
        else if (c >= 32 && c < 127) o << c;
        else o << '?';
    }
    o << '"'; return o.str();
}
inline void tradeJson(std::ostream& o, const Trade& t) {
    o << "{\"sequence\":" << t.sequence << ",\"buy\":" << t.buy << ",\"sell\":" << t.sell
      << ",\"maker\":" << t.maker << ",\"taker\":" << t.taker
      << ",\"price\":" << t.price << ",\"quantity\":" << t.quantity << '}';
}
inline std::string json(const Engine& e, const Result& r) {
    std::ostringstream o;
    o << "{\"ok\":" << (r.ok ? "true" : "false") << ",\"status\":" << escaped(r.status)
      << ",\"message\":" << escaped(r.message) << ",\"id\":" << r.id
      << ",\"filled\":" << r.filled << ",\"resting\":" << r.resting << ",\"cancelled\":" << r.cancelled
      << ",\"new_trades\":[";
    bool comma = false;
    for (const auto& t : r.trades) { if (comma) o << ','; tradeJson(o,t); comma = true; }
    o << "],\"book\":{\"bids\":[";
    for (Side side : {Side::Buy, Side::Sell}) {
        if (side == Side::Sell) o << "],\"asks\":[";
        comma = false; std::size_t count = 0;
        for (const auto& d : e.depth(side)) {
            if (count++ == 100) break;
            if (comma) o << ',';
            o << "{\"price\":" << d.price << ",\"quantity\":" << d.quantity << ",\"orders\":" << d.orders << '}';
            comma = true;
        }
    }
    o << "],\"active_count\":" << e.activeCount() << ",\"trade_count\":" << e.tradeCount()
      << ",\"volume\":" << e.volume() << ",\"orders\":[";
    comma = false; std::size_t count = 0;
    for (const auto& order : e.orders()) {
        if (count++ == 200) break;
        if (comma) o << ',';
        o << "{\"id\":" << order.id << ",\"side\":" << escaped(name(order.side)) << ",\"price\":" << order.price
          << ",\"remaining\":" << order.remaining << ",\"sequence\":" << order.sequence << '}';
        comma = true;
    }
    o << "],\"trades\":["; comma = false;
    for (const auto& t : e.recent()) { if (comma) o << ','; tradeJson(o,t); comma = true; }
    o << "]}}"; return o.str();
}
inline Result execute(Engine& e, const std::string& command) {
    try {
        const auto w = tokens(command);
        if (w.size() == 1 && w[0] == "BOOK") { Result r; r.ok = true; r.status = "SNAPSHOT"; return r; }
        if (w.size() == 1 && w[0] == "RESET") { e.clear(); Result r; r.ok = true; r.status = "RESET"; return r; }
        if (w.size() == 2 && w[0] == "CANCEL") return e.cancel(integer<Id>(w[1]));
        return e.submit(request(w));
    } catch (const std::invalid_argument& error) {
        Result r; r.status = "REJECTED"; r.message = error.what(); return r;
    }
}
} // namespace exchange
