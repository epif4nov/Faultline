#include "faultline/simulator.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>

static unsigned number(const char* text) {
    unsigned value{};
    std::string s(text);
    auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (error != std::errc{} || end != s.data() + s.size()) throw std::runtime_error("expected unsigned integer: " + s);
    return value;
}
int main(int argc, char** argv) {
    using namespace faultline;
    try {
        if (argc == 1 || std::string(argv[1]) == "--help") {
            std::cout << "Faultline — deterministic quorum fault laboratory\n"
                         "  faultline replay SCENARIO R [TRACE.json]\n"
                         "  faultline search FIRST_SEED COUNT R OUTPUT_PREFIX\n"
                         "R=1 exposes stale reads; R=2 intersects W=2 (N=3).\n"
                         "Exit: 0 no violation, 1 violation found, 2 usage/I/O error.\n";
            return 0;
        }
        const std::string command(argv[1]);
        if (command == "replay" && (argc == 4 || argc == 5)) {
            std::ifstream in(argv[2]);
            if (!in) throw std::runtime_error("cannot open scenario");
            const auto result = Simulator(static_cast<int>(number(argv[3]))).run(parse(in));
            if (argc == 5) {
                std::ofstream out(argv[4]); out << json(result);
                if (!out) throw std::runtime_error("cannot write trace");
            }
            std::cout << "reads=" << result.reads << " commits=" << result.commits << " violated=" << result.violated << '\n';
            return result.violated ? 1 : 0;
        }
        if (command == "search" && argc == 6) {
            const auto first = number(argv[2]), count = number(argv[3]);
            const int quorum = static_cast<int>(number(argv[4]));
            if (quorum != 1 && quorum != 2) throw std::runtime_error("R must be 1 or 2");
            if (count == 0 || count > 1000000 || first > std::numeric_limits<unsigned>::max() - (count - 1))
                throw std::runtime_error("invalid seed range (count must be 1..1000000)");
            std::uint64_t reads = 0, commits = 0;
            for (unsigned i = 0; i < count; ++i) {
                auto actions = generate(first + i);
                const auto result = Simulator(quorum).run(actions);
                reads += result.reads; commits += result.commits;
                if (!result.violated) continue;
                auto reduced = minimize(actions, quorum);
                const std::string prefix(argv[5]);
                std::ofstream original(prefix + ".original.fl"), minimized(prefix + ".fl"), trace(prefix + ".json");
                original << format(actions); minimized << format(reduced); trace << json(Simulator(quorum).run(reduced));
                if (!original || !minimized || !trace) throw std::runtime_error("cannot write search artifacts");
                std::cout << "seed=" << first + i << " actions=" << actions.size() << " minimized=" << reduced.size() << '\n';
                return 1;
            }
            std::cout << "checked=" << count << " reads=" << reads << " commits=" << commits << " violations=0\n";
            return 0;
        }
        throw std::runtime_error("invalid arguments; use --help");
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 2; }
}
