#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace faultline {
enum class Kind { write, read, down, up, tick };
struct Action { Kind kind; int argument = 0; bool operator==(const Action&) const = default; };
using Scenario = std::vector<Action>;
inline std::string format(const Scenario& actions) {
    std::ostringstream out;
    for (auto a : actions) {
        switch (a.kind) {
        case Kind::write: out << "write " << a.argument; break;
        case Kind::read: out << "read"; break;
        case Kind::down: out << "down " << a.argument; break;
        case Kind::up: out << "up " << a.argument; break;
        case Kind::tick: out << "tick " << a.argument; break;
        }
        out << '\n';
    }
    return out.str();
}
inline Scenario parse(std::istream& input) {
    Scenario result;
    std::string line;
    int number = 0;
    while (std::getline(input, line)) {
        ++number;
        line = line.substr(0, line.find('#'));
        std::istringstream row(line);
        std::string command, extra;
        if (!(row >> command)) continue;
        Action a{};
        if (command == "read") a.kind = Kind::read;
        else {
            if (command == "write") a.kind = Kind::write;
            else if (command == "down") a.kind = Kind::down;
            else if (command == "up") a.kind = Kind::up;
            else if (command == "tick") a.kind = Kind::tick;
            else throw std::runtime_error("unknown command on line " + std::to_string(number));
            if (!(row >> a.argument)) throw std::runtime_error("expected integer on line " + std::to_string(number));
            if ((a.kind == Kind::up || a.kind == Kind::down) && (a.argument < 0 || a.argument > 2))
                throw std::runtime_error("replica must be 0..2");
            if (a.kind == Kind::tick && (a.argument < 0 || a.argument > 1000000))
                throw std::runtime_error("tick must be 0..1000000");
        }
        if (row >> extra) throw std::runtime_error("unexpected argument on line " + std::to_string(number));
        result.push_back(a);
    }
    return result;
}
struct Record {
    std::uint64_t time;
    std::string type, detail;
    std::array<int, 3> versions;
    std::array<bool, 3> online;
    int committed;
};
struct Result {
    bool violated = false;
    int reads = 0, commits = 0;
    std::vector<Record> trace;
};
// A central test driver submits operations to a replicated versioned register.
// One request and one reply leg per replica; no wall clocks or real sockets.
class Simulator {
    struct Replica { int version = 0, value = 0; bool online = true; };
    struct Event {
        std::uint64_t time, sequence;
        std::function<void()> execute;
        bool operator>(const Event& other) const {
            return time > other.time || (time == other.time && sequence > other.sequence);
        }
    };
    struct Operation { int responses = 0, best = 0, floor = 0; bool done = false; };
    std::array<Replica, 3> replicas_{};
    std::array<int, 3> latency_{2, 3, 1};
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> events_;
    std::map<int, Operation> writes_, reads_;
    std::uint64_t now_ = 0, sequence_ = 0;
    int next_write_ = 0, next_read_ = 0, committed_ = 0, read_quorum_;
    Result result_;
    void log(std::string type, std::string detail) {
        result_.trace.push_back({now_, std::move(type), std::move(detail),
            {replicas_[0].version, replicas_[1].version, replicas_[2].version},
            {replicas_[0].online, replicas_[1].online, replicas_[2].online}, committed_});
    }
    void schedule(int delay, std::function<void()> fn) {
        events_.push({now_ + static_cast<std::uint64_t>(delay), sequence_++, std::move(fn)});
    }
    void advance(int ticks) {
        const auto end = now_ + static_cast<std::uint64_t>(ticks);
        while (!events_.empty() && events_.top().time <= end) {
            auto event = events_.top(); events_.pop(); now_ = event.time; event.execute();
        }
        now_ = end;
    }
    void write(int value) {
        const int version = ++next_write_;
        writes_[version] = {};
        log("write", "submit v" + std::to_string(version) + " = " + std::to_string(value));
        for (int node = 0; node < 3; ++node) {
            if (!replicas_[node].online) { log("drop", "write to offline replica " + std::to_string(node)); continue; }
            schedule(latency_[node], [this, node, version, value] {
                auto& replica = replicas_[node];
                if (!replica.online) { log("drop", "write delivery to replica " + std::to_string(node)); return; }
                if (version > replica.version) { replica.version = version; replica.value = value; }
                log("deliver", "replica " + std::to_string(node) + " accepted v" + std::to_string(version));
                schedule(latency_[node], [this, version] {
                    auto& op = writes_.at(version);
                    if (++op.responses >= 2 && !op.done) {
                        op.done = true; committed_ = std::max(committed_, version); ++result_.commits;
                        log("commit", "write v" + std::to_string(version) + " acknowledged by W=2");
                    }
                });
            });
        }
    }
    void read() {
        const int id = ++next_read_;
        reads_[id] = {0, 0, committed_, false};
        log("read", "submit read " + std::to_string(id) + "; required version >= " + std::to_string(committed_));
        for (int node = 0; node < 3; ++node) {
            if (!replicas_[node].online) continue;
            schedule(latency_[node], [this, node, id] {
                if (!replicas_[node].online) { log("drop", "read delivery to replica " + std::to_string(node)); return; }
                const int version = replicas_[node].version;
                schedule(latency_[node], [this, id, version, node] {
                    auto& op = reads_.at(id);
                    if (op.done) return;
                    op.best = std::max(op.best, version); ++op.responses;
                    log("reply", "replica " + std::to_string(node) + " returned v" + std::to_string(version));
                    if (op.responses >= read_quorum_) {
                        op.done = true; ++result_.reads;
                        const bool bad = op.best < op.floor;
                        result_.violated |= bad;
                        log(bad ? "violation" : "read_ok", "read " + std::to_string(id) + " returned v" + std::to_string(op.best) + "; required >= " + std::to_string(op.floor));
                    }
                });
            });
        }
    }
public:
    explicit Simulator(int read_quorum) : read_quorum_(read_quorum) {
        if (read_quorum != 1 && read_quorum != 2) throw std::invalid_argument("R must be 1 or 2");
    }
    Result run(const Scenario& actions) {
        for (auto a : actions) {
            switch (a.kind) {
            case Kind::write: write(a.argument); break;
            case Kind::read: read(); break;
            case Kind::tick: advance(a.argument); break;
            case Kind::down: case Kind::up:
                if (a.argument < 0 || a.argument > 2) throw std::invalid_argument("replica must be 0..2");
                replicas_[a.argument].online = a.kind == Kind::up;
                log(a.kind == Kind::up ? "up" : "down", "replica " + std::to_string(a.argument)); break;
            }
        }
        log("end", "completed reads=" + std::to_string(result_.reads) + ", committed writes=" + std::to_string(result_.commits));
        return result_;
    }
};
inline Scenario generate(std::uint32_t seed, int steps = 80) {
    std::mt19937 rng(seed);
    Scenario actions;
    for (int i = 0; i < steps; ++i) {
        switch (rng() % 5) {
        case 0: actions.push_back({Kind::down, static_cast<int>(rng() % 3)}); break;
        case 1: actions.push_back({Kind::up, static_cast<int>(rng() % 3)}); break;
        case 2: actions.push_back({Kind::write, i}); break;
        case 3: actions.push_back({Kind::read}); break;
        default: actions.push_back({Kind::tick, static_cast<int>(rng() % 10)}); break;
        }
    }
    actions.push_back({Kind::tick, 20});
    return actions;
}
// Deletion shrinking to a fixed point: every remaining individual action matters.
inline Scenario minimize(Scenario actions, int quorum) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < actions.size();) {
            auto candidate = actions;
            candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(i));
            if (Simulator(quorum).run(candidate).violated) { actions = std::move(candidate); changed = true; }
            else ++i;
        }
    }
    return actions;
}
inline std::string escape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c == '\n') out << "\\n";
        else if (c < 32) { const char* hex = "0123456789abcdef"; out << "\\u00" << hex[c >> 4] << hex[c & 15]; }
        else out << c;
    }
    return out.str();
}
inline std::string json(const Result& result) {
    std::ostringstream out;
    out << "{\"violated\":" << (result.violated ? "true" : "false") << ",\"reads\":" << result.reads << ",\"commits\":" << result.commits << ",\"trace\":[";
    bool first = true;
    for (const auto& e : result.trace) {
        if (!first) out << ',';
        first = false;
        out << "{\"time\":" << e.time << ",\"type\":\"" << escape(e.type) << "\",\"detail\":\"" << escape(e.detail) << "\",\"committed\":" << e.committed << ",\"versions\":[";
        for (int i = 0; i < 3; ++i) out << (i ? "," : "") << e.versions[i];
        out << "],\"online\":[";
        for (int i = 0; i < 3; ++i) out << (i ? "," : "") << (e.online[i] ? "true" : "false");
        out << "]}";
    }
    return out.str() + "]}\n";
}
}
