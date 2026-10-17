# Design decisions

1. **Simulation before real sockets.** A socket-based demo hides failures behind timing. Virtual time makes every event replayable and makes reduction practical. It deliberately omits operating-system and transport behavior.
2. **Quorum register before consensus.** The six-line witness exposes a real systems tradeoff without claiming an incomplete Raft implementation. The version-floor property is narrow enough to state and inspect precisely.
3. **Monotonic driver versions.** A single driver issues unique ordered versions, so clock synchronization is outside the model. This is a simplifying assumption, not a proposed distributed timestamp service.
4. **No dependency graph for a tiny binary.** A Makefile and C++ standard library are sufficient. Python is used for integration tests and artifact generation; it is not in the simulation hot path. Browser code uses native DOM APIs.
5. **Observable results over a green exit alone.** Reports include completed read/write counts. Tests enforce nonzero exercise and expect a nonzero exit for known bad scenarios.
