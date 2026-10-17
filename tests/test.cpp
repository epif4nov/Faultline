#include "faultline/simulator.hpp"
#include <iostream>
using namespace faultline;
static int checks = 0;
static void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
int main() {
    try {
        std::istringstream text("down 2\nwrite 42\ntick 10\nup 2\nread\ntick 10\n");
        auto scenario = parse(text);
        auto bad = Simulator(1).run(scenario), good = Simulator(2).run(scenario);
        check(bad.violated && bad.reads == 1 && bad.commits == 1, "R1 must expose stale read");
        check(!good.violated && good.reads == 1 && good.commits == 1, "R2 must read latest committed version");
        check(json(bad) == json(Simulator(1).run(scenario)), "replay must be byte deterministic");
        auto small = minimize(scenario, 1);
        check(Simulator(1).run(small).violated, "shrinker must preserve failure");
        for (std::size_t i = 0; i < small.size(); ++i) {
            auto candidate = small; candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(i));
            check(!Simulator(1).run(candidate).violated, "shrinker must be deletion minimal");
        }
        std::istringstream roundtrip(format(small));
        check(parse(roundtrip) == small, "scenario roundtrip");
        for (const std::string invalid : {"down 3", "tick -1", "read x", "write", "explode", "tick 99999999999"}) {
            bool rejected = false;
            try { std::istringstream in(invalid); (void)parse(in); } catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "malformed scenario accepted");
        }
        auto unavailable = Simulator(2).run({{Kind::down,0},{Kind::down,1},{Kind::write,7},{Kind::read},{Kind::tick,20}});
        check(unavailable.commits == 0 && unavailable.reads == 0 && !unavailable.violated, "minority must not complete quorum operations");
        auto dropped = Simulator(2).run({{Kind::write,1},{Kind::down,0},{Kind::down,1},{Kind::tick,20}});
        check(dropped.commits == 0, "offline nodes must drop in-flight requests");
        int total_reads = 0, failures = 0;
        for (std::uint32_t seed = 0; seed < 2000; ++seed) {
            auto generated = generate(seed);
            auto result = Simulator(2).run(generated);
            check(!result.violated, "quorum intersection violated");
            total_reads += result.reads;
            failures += Simulator(1).run(generated).violated ? 1 : 0;
        }
        check(total_reads > 1000 && failures > 0, "random corpus must exercise meaningful outcomes");
        std::cout << checks << " checks passed; 2000 seeds; " << total_reads << " quorum reads; " << failures << " unsafe seeds\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
