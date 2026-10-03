#include <iostream>
#include "protocol.hpp"
#include <fstream>
#include <memory>
using namespace exchange;
int main(int argc, char** argv) {
    try {
        std::ifstream file;
        std::istream* source = &std::cin;
        if (argc == 3 && std::string(argv[1]) == "--replay") {
            file.open(argv[2]); if (!file) throw std::runtime_error("Cannot open replay file"); source = &file;
        } else if (argc != 1 && !(argc == 2 && std::string(argv[1]) == "--json")) {
            std::cerr << "Usage: exchange [--json | --replay events.txt]\n"; return 1;
        }
        Engine engine;
        for (std::string line; std::getline(*source, line);) {
            if (line.empty() || line[0] == '#') continue;
            if (line == "QUIT") break;
            if (line.size() > 512) {
                Result r; r.status = "REJECTED"; r.message = "Command too long";
                std::cout << json(engine,r) << std::endl; continue;
            }
            Result result = execute(engine, line);
            std::cout << json(engine, result) << std::endl;
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "Fatal: " << e.what() << '\n'; return 1; }
}
