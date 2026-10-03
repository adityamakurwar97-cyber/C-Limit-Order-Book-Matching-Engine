#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

enum class Side {
    Buy,
    Sell
};

struct Order {
    std::uint64_t id;
    Side side;
    std::int64_t price_ticks;
    std::int64_t remaining_quantity;
    std::uint64_t sequence;
};

struct Trade {
    std::uint64_t buy_id;
    std::uint64_t sell_id;
    std::int64_t price_ticks;
    std::int64_t quantity;
};

// Converts digits into an integer without using from_chars.
// Negative numbers, extra characters and overflow are rejected.
template <typename T>
T number(const std::string& token) {
    if (token.empty()) {
        throw std::invalid_argument("Empty integer");
    }

    T value = 0;
    const T maximum = std::numeric_limits<T>::max();

    for (char character : token) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument(
                "Expected a non-negative integer: " + token
            );
        }

        const T digit = static_cast<T>(character - '0');

        // Check before multiplying, so overflow never happens.
        if (value > (maximum - digit) / 10) {
            throw std::invalid_argument(
                "Integer too large: " + token
            );
        }

        value = static_cast<T>(value * 10 + digit);
    }

    return value;
}

class OrderBook {
private:
    std::vector<Order> bids_;
    std::vector<Order> asks_;
    std::vector<Trade> trades_;

    // Prevent ID reuse, even after an order fills or is cancelled.
    std::unordered_set<std::uint64_t> used_ids_;

    std::uint64_t sequence_ = 0;

    static bool better(const Order& first, const Order& second) {
        if (first.price_ticks != second.price_ticks) {
            if (first.side == Side::Buy) {
                return first.price_ticks > second.price_ticks;
            }

            return first.price_ticks < second.price_ticks;
        }

        // Same price: earlier arrival gets priority.
        return first.sequence < second.sequence;
    }

    static std::size_t bestIndex(const std::vector<Order>& orders) {
        if (orders.empty()) {
            throw std::logic_error("Cannot search an empty side");
        }

        std::size_t best_index = 0;

        for (std::size_t i = 1; i < orders.size(); ++i) {
            if (better(orders[i], orders[best_index])) {
                best_index = i;
            }
        }

        return best_index;
    }

    static void printSide(
        const std::vector<Order>& orders,
        const std::string& label
    ) {
        std::cout << "\n" << label << "\n";
        std::cout << "ID\tPRICE\tQTY\tSEQUENCE\n";

        if (orders.empty()) {
            std::cout << "(empty)\n";
            return;
        }

        // Sort a copy only for display.
        std::vector<Order> sorted = orders;
        std::sort(sorted.begin(), sorted.end(), better);

        for (const Order& order : sorted) {
            std::cout
                << order.id << '\t'
                << order.price_ticks << '\t'
                << order.remaining_quantity << '\t'
                << order.sequence << '\n';
        }
    }

public:
    const std::vector<Order>& bids() const {
        return bids_;
    }

    const std::vector<Order>& asks() const {
        return asks_;
    }

    const std::vector<Trade>& trades() const {
        return trades_;
    }

    void add(
        std::uint64_t id,
        Side side,
        std::int64_t price,
        std::int64_t quantity
    ) {
        if (id == 0 || price <= 0 || quantity <= 0) {
            throw std::invalid_argument(
                "ID, price and quantity must be positive"
            );
        }

        if (side != Side::Buy && side != Side::Sell) {
            throw std::invalid_argument("Invalid order side");
        }

        if (used_ids_.count(id) != 0) {
            throw std::invalid_argument(
                "Order ID already used this session"
            );
        }

        if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("Sequence number exhausted");
        }

        used_ids_.insert(id);

        Order incoming{
            id,
            side,
            price,
            quantity,
            ++sequence_
        };

        // Reference to the opposite vector, not a copy.
        std::vector<Order>& opposite =
            side == Side::Buy ? asks_ : bids_;

        while (
            incoming.remaining_quantity > 0 &&
            !opposite.empty()
        ) {
            const std::size_t index = bestIndex(opposite);

            // Reference lets us update the actual stored order.
            Order& resting = opposite[index];

            const bool can_match =
                side == Side::Buy
                    ? incoming.price_ticks >= resting.price_ticks
                    : incoming.price_ticks <= resting.price_ticks;

            if (!can_match) {
                break;
            }

            const std::int64_t trade_quantity = std::min(
                incoming.remaining_quantity,
                resting.remaining_quantity
            );

            Trade trade{
                side == Side::Buy ? incoming.id : resting.id,
                side == Side::Sell ? incoming.id : resting.id,
                resting.price_ticks,
                trade_quantity
            };

            trades_.push_back(trade);

            incoming.remaining_quantity -= trade_quantity;
            resting.remaining_quantity -= trade_quantity;

            if (resting.remaining_quantity == 0) {
                opposite.erase(
                    opposite.begin() +
                    static_cast<std::vector<Order>::difference_type>(
                        index
                    )
                );

                // Do not use resting after erase.
                // Next iteration searches for the best order again.
            }
        }

        // Unfilled quantity stays at its original limit price.
        if (incoming.remaining_quantity > 0) {
            if (side == Side::Buy) {
                bids_.push_back(incoming);
            } else {
                asks_.push_back(incoming);
            }
        }
    }

    bool cancel(std::uint64_t id) {
        for (std::size_t i = 0; i < bids_.size(); ++i) {
            if (bids_[i].id == id) {
                bids_.erase(
                    bids_.begin() +
                    static_cast<std::vector<Order>::difference_type>(i)
                );
                return true;
            }
        }

        for (std::size_t i = 0; i < asks_.size(); ++i) {
            if (asks_[i].id == id) {
                asks_.erase(
                    asks_.begin() +
                    static_cast<std::vector<Order>::difference_type>(i)
                );
                return true;
            }
        }

        return false;
    }

    void printBook() const {
        printSide(bids_, "BIDS - highest price first");
        printSide(asks_, "ASKS - lowest price first");

        if (!bids_.empty()) {
            std::cout << "\nBest bid ticks: "
                      << bids_[bestIndex(bids_)].price_ticks
                      << '\n';
        }

        if (!asks_.empty()) {
            std::cout << "Best ask ticks: "
                      << asks_[bestIndex(asks_)].price_ticks
                      << '\n';
        }

        if (!bids_.empty() && !asks_.empty()) {
            const std::int64_t spread =
                asks_[bestIndex(asks_)].price_ticks -
                bids_[bestIndex(bids_)].price_ticks;

            std::cout << "Spread ticks: " << spread << '\n';
        }
    }

    void printTrades(std::size_t start = 0) const {
        if (trades_.empty()) {
            std::cout << "No trades\n";
            return;
        }

        for (std::size_t i = start; i < trades_.size(); ++i) {
            const Trade& trade = trades_[i];

            std::cout
                << "TRADE"
                << " buy=" << trade.buy_id
                << " sell=" << trade.sell_id
                << " price_ticks=" << trade.price_ticks
                << " quantity=" << trade.quantity
                << '\n';
        }
    }
};

void printHelp() {
    std::cout
        << "\nCommands:\n"
        << "BUY id price_ticks quantity\n"
        << "SELL id price_ticks quantity\n"
        << "CANCEL id\n"
        << "BOOK\n"
        << "TRADES\n"
        << "HELP\n"
        << "QUIT\n\n"
        << "Example: SELL 1 10000 5\n"
        << "Example: BUY 2 10001 3\n\n"
        << "Use uppercase commands and positive integers.\n"
        << "One instrument per book. Prices are integer ticks.\n"
        << "IDs cannot be reused during the same session.\n";
}

void runDemo() {
    OrderBook book;

    // Your orders submitted in arrival sequence.
    book.add(1, Side::Sell, 10000, 5);
    book.add(2, Side::Buy, 10000, 3);
    book.add(3, Side::Buy, 10001, 2);
    book.add(4, Side::Buy, 9999, 1);
    book.add(5, Side::Sell, 10001, 2);
    book.add(6, Side::Sell, 9999, 1);

    std::cout << "\nDEMO TRADES\n";
    book.printTrades();

    std::cout << "\nFINAL ORDER BOOK\n";
    book.printBook();
}

// Unlike assert(), these checks also run in release builds.
void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("Test failed: " + message);
    }
}

template <typename Function>
void expectRejected(Function function) {
    bool rejected = false;

    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    check(rejected, "invalid input should be rejected");
}

void runTests() {
    int passed = 0;

    {
        OrderBook book;
        check(book.bids().empty(), "initial bids empty");
        check(book.asks().empty(), "initial asks empty");
        check(!book.cancel(99), "unknown cancellation");
        ++passed;
    }

    {
        OrderBook book;
        book.add(1, Side::Buy, 99, 2);
        book.add(2, Side::Sell, 100, 3);

        check(book.trades().empty(), "non-crossing prices");
        check(book.bids().size() == 1, "buy rests");
        check(book.asks().size() == 1, "sell rests");
        ++passed;
    }

    {
        OrderBook book;
        book.add(1, Side::Sell, 100, 2);
        book.add(2, Side::Buy, 100, 2);

        check(book.trades().size() == 1, "one full trade");
        check(book.trades()[0].quantity == 2, "full quantity");
        check(book.bids().empty() && book.asks().empty(),
              "fully filled orders removed");
        ++passed;
    }

    {
        OrderBook book;
        book.add(1, Side::Sell, 100, 5);
        book.add(2, Side::Buy, 105, 2);

        check(book.asks().size() == 1, "seller still active");
        check(book.asks()[0].remaining_quantity == 3,
              "seller partial remainder");
        check(book.trades()[0].price_ticks == 100,
              "resting sell execution price");
        ++passed;
    }

    {
        OrderBook book;
        book.add(1, Side::Buy, 105, 2);
        book.add(2, Side::Sell, 100, 5);

        check(book.bids().empty(), "buyer filled");
        check(book.asks().size() == 1, "incoming sell rests");
        check(book.asks()[0].remaining_quantity == 3,
              "incoming sell remainder");
        check(book.asks()[0].price_ticks == 100,
              "remainder retains limit");
        check(book.trades()[0].price_ticks == 105,
              "resting buy execution price");
        ++passed;
    }

    {
        OrderBook book;
        book.add(1, Side::Sell, 102, 2);
        book.add(2, Side::Sell, 100, 3);
        book.add(3, Side::Buy, 102, 4);

        check(book.trades().size() == 2, "multiple fills");
        check(book.trades()[0].sell_id == 2,
              "lower sell price first");
        check(book.trades()[1].sell_id == 1,
              "next sell price second");
        check(book.asks()[0].remaining_quantity == 1,
              "remaining seller quantity");
        ++passed;
    }

    {
        OrderBook book;
        book.add(1, Side::Buy, 100, 1);
        book.add(2, Side::Buy, 102, 1);
        book.add(3, Side::Sell, 100, 1);

        check(book.trades().size() == 1, "one sell execution");
        check(book.trades()[0].buy_id == 2,
              "higher buy price first");
        ++passed;
    }

    {
        OrderBook book;
        book.add(10, Side::Sell, 100, 3);
        book.add(2, Side::Sell, 100, 2);
        book.add(3, Side::Buy, 100, 1);
        book.add(4, Side::Buy, 100, 3);

        check(book.trades().size() == 3, "three FIFO trades");
        check(book.trades()[0].sell_id == 10, "arrival beats ID");
        check(book.trades()[1].sell_id == 10,
              "partial fill keeps priority");
        check(book.trades()[1].quantity == 2,
              "first seller remainder filled");
        check(book.trades()[2].sell_id == 2,
              "second seller next");
        ++passed;
    }

    {
        OrderBook book;
        book.add(1, Side::Sell, 100, 2);
        book.add(2, Side::Sell, 103, 2);
        book.add(3, Side::Buy, 101, 5);

        check(book.trades().size() == 1, "stop at limit");
        check(book.bids().size() == 1, "buy remainder rests");
        check(book.bids()[0].remaining_quantity == 3,
              "correct buy remainder");
        check(book.asks()[0].price_ticks == 103,
              "expensive ask remains");
        ++passed;
    }

    {
        OrderBook book;
        book.add(1, Side::Buy, 99, 2);
        book.add(2, Side::Sell, 101, 2);

        check(book.cancel(1), "cancel buy");
        check(book.cancel(2), "cancel sell");
        check(!book.cancel(2), "cannot cancel twice");

        expectRejected([&book]() {
            book.add(1, Side::Buy, 99, 2);
        });
        ++passed;
    }

    {
        OrderBook book;

        expectRejected([&book]() {
            book.add(0, Side::Buy, 100, 1);
        });

        expectRejected([&book]() {
            book.add(1, Side::Buy, 0, 1);
        });

        expectRejected([&book]() {
            book.add(1, Side::Buy, 100, -1);
        });

        expectRejected([&book]() {
            book.add(1, static_cast<Side>(7), 100, 1);
        });

        book.add(1, Side::Buy, 100, 1);

        expectRejected([&book]() {
            book.add(1, Side::Sell, 100, 1);
        });

        check(book.bids().size() == 1 && book.asks().empty(),
              "rejected orders do not change book");

        book.add(2, Side::Sell, 100, 1);

        expectRejected([&book]() {
            book.add(2, Side::Sell, 100, 1);
        });
        ++passed;
    }

    {
        expectRejected([]() {
            (void)number<std::uint64_t>("-1");
        });

        expectRejected([]() {
            (void)number<std::int64_t>("12abc");
        });

        expectRejected([]() {
            (void)number<std::int64_t>("9223372036854775808");
        });

        expectRejected([]() {
            (void)number<std::uint64_t>("18446744073709551616");
        });

        expectRejected([]() {
            (void)number<std::int64_t>("");
        });

        check(number<std::int64_t>("123") == 123,
              "normal integer conversion");

        OrderBook book;
        const std::int64_t maximum =
            std::numeric_limits<std::int64_t>::max();

        book.add(1, Side::Sell, maximum, maximum);
        book.add(2, Side::Buy, maximum, maximum);

        check(book.trades()[0].quantity == maximum,
              "maximum supported quantity");
        check(book.asks().empty(), "large order fully filled");
        ++passed;
    }

    std::cout << passed << " test groups passed\n";
}

void runInteractive() {
    OrderBook book;
    printHelp();

    std::string line;

    while (true) {
        std::cout << "\n> ";

        if (!std::getline(std::cin, line)) {
            break;
        }

        std::istringstream input(line);
        std::vector<std::string> words;
        std::string word;

        while (input >> word) {
            words.push_back(word);
        }

        if (words.empty()) {
            continue;
        }

        try {
            const std::string& command = words[0];

            if (
                (command == "BUY" || command == "SELL") &&
                words.size() == 4
            ) {
                const std::uint64_t id =
                    number<std::uint64_t>(words[1]);

                const std::int64_t price =
                    number<std::int64_t>(words[2]);

                const std::int64_t quantity =
                    number<std::int64_t>(words[3]);

                const Side side =
                    command == "BUY" ? Side::Buy : Side::Sell;

                const std::size_t previous_trades =
                    book.trades().size();

                book.add(id, side, price, quantity);

                std::cout << "Accepted order " << id << '\n';

                if (book.trades().size() > previous_trades) {
                    book.printTrades(previous_trades);
                }
            } else if (command == "CANCEL" && words.size() == 2) {
                const std::uint64_t id =
                    number<std::uint64_t>(words[1]);

                if (book.cancel(id)) {
                    std::cout << "Cancelled order " << id << '\n';
                } else {
                    std::cout << "No active order with that ID\n";
                }
            } else if (command == "BOOK" && words.size() == 1) {
                book.printBook();
            } else if (command == "TRADES" && words.size() == 1) {
                book.printTrades();
            } else if (command == "HELP" && words.size() == 1) {
                printHelp();
            } else if (command == "QUIT" && words.size() == 1) {
                break;
            } else {
                throw std::invalid_argument(
                    "Unknown command or wrong argument count. Use HELP."
                );
            }
        } catch (const std::invalid_argument& error) {
            std::cout << "Rejected: " << error.what() << '\n';
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        if (argc == 2) {
            const std::string option = argv[1];

            if (option == "--test") {
                runTests();
                return 0;
            }

            if (option == "--demo") {
                runDemo();
                return 0;
            }
        }

        if (argc != 1) {
            std::cerr << "Usage: lob [--test|--demo]\n";
            return 1;
        }

        runInteractive();
        return 0;

    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}