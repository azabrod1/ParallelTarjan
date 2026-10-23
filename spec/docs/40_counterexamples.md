## Counterexamples in the OLD flag protocol (8a9f1a9)

The full TLC traces are in `results/TerminationFlags_*.out`; condensed versions are in
`results/TerminationFlags_*.trace.txt`. Workers are w1..w3, and searches are named by their root.

### (a) Safety: early exit with a stale bit (`TerminationFlags_safety`, `NoEarlyExit` violated, 24 states)

1. w1 enters `getSearch`, clears `flags`, claims root 1 and runs search 1 (states 2-5); from then on root 2's vertex may already belong to search 1. w1 finishes (6), re-enters `getSearch`, clears (7), and takes root 2 from the queue (8-9).
2. w2 enters `getSearch` and clears `flags` (10). w1's `claimOrFail` on root 2 fails, because search 1 owns vertex 2 (11). w3 enters and clears (12).
3. w1 runs `flags |= mask` (13): `flags = {w1}`. The clears by w2 and w3 landed between w1's empty checks and its bit store. w1 sees "not all set" and sleeps (14-15).
4. w1 wakes, finds Pending empty, and takes root 3 (16-17). **Its bit is still set**, although w1 is about to start work.
5. w2 and w3 find Pending empty and the queue exhausted, and set their bits (18-23): `flags = {w1, w2, w3}`.
6. w2 reads all bits set and **exits** (24) while w1 holds root 3 and is about to claim and run it.

This is the mechanism audit §8 describes: the bit is set after the empty checks rather than
atomically with them. The work w1 takes is a new root, not a Pending search, which is incidental.
The trace is realizable: with `RealisticRoots = TRUE`, root 2 can lose its claim only because
search 1 had started.

A simpler route exists too, though the audit does not list it. A worker whose root claim fails sets
its bit at once, with no exhaustion check. On its next iteration it can claim another root with the
bit still set. `TerminationFlags_live` reaches this route (see (c)).

**Consequence.** `TerminationFlags_alldone` checks `AllExitedAllDone` alone, and it **passes**. In
this abstraction the early exit never ends the run with unfinished searches. The result is lost
parallelism and the hang in (c): once a worker has exited, the next clear erases its bit for good.

### (b) Liveness: one stalled idle worker hangs everybody (`TerminationFlags_stall_only`, 30 states, lasso back to state 21)

The config has 1 root, no suspensions and `MaxStalls = 1`. Neither stale-bit route can occur here,
and `NoStaleBit` and `NoEarlyExit` hold in the same run. The failure is therefore caused by the stall
alone.

1. w1 claims root 1, runs search 1 and finishes it (states 2-11).
2. w3 sees an empty queue and sets its bit (13): `flags = {w3}`. It sees "not all set" and enters `sleep_for` (14).
3. w1 re-enters `getSearch` and clears `flags` (15).
4. **w3 stalls inside `sleep_for` forever** (16).
5. w1 and w2 set their bits (`flags = {w1, w2}`) and poll forever; w3's bit is never set again (17-30, back to 21).

Every search is done, yet nobody exits. This is the hang the audit measured: one worker sat in
`sleep_for(1ms)` while 62 others polled. `TerminationFlags_stall` (3 roots, 3 suspensions, 1 stall)
fails Termination with a trace of the same shape. `TerminationFlags_nostall_small` has the same tiny
constants without stalls, and it passes.

### (c) Liveness without any stall (`TerminationFlags_live`, lasso back to state 43)

1. w2's claim fails and w2 sets its bit (14). It then claims root 3 with the bit still set and runs search 3 (19-22).
2. w1 and w3 set their bits, so `flags = {w1, w2, w3}`, and w1 **exits** (26) while w2 is between finishing search 3 and its next `flags.store(0)`.
3. w2's clear (27) erases w1's bit permanently. w2 and w3 loop forever.

### Comparison with the counter protocols

The new protocols pass the same properties under the same search abstraction, including any number
of workers stalled in `sleep_for`:
- `Termination.tla` (b9d4ba4)
- `TerminationBlocks.tla` (2a280d9)
