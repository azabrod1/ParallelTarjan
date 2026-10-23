## Results at a glance

No counterexample was found in the **current** protocols: the counter termination at b9d4ba4 and
2a280d9, the suspend/complete hand-shake, and cycle detection with consensus at 2a280d9. Every PASS
holds only within the bounds in the table below. The old flag protocol (8a9f1a9) fails as the audit
predicted: (a) safety and (b) liveness counterexamples were found, plus (c) a hang that needs no
stall.

| Model | Property | Result |
| --- | --- | --- |
| `Termination` (b9d4ba4) | `CountInv`, `NoEarlyExit`, `DoneOnExit`, `AllExitedAllDone` | PASS (3 workers, 3 roots, 3 suspensions; plus 4 roots / 4 suspensions safety-only) |
| | `Termination` (liveness, WF + SF on dequeue), with **any** number of workers stalled in `sleep_for` | PASS (3 workers, 3 roots; 4 roots in the long run) |
| | mutations: enqueue before `+=k`; take root before `+=1` | both caught (`NoEarlyExit`) |
| `TerminationBlocks` (2a280d9) | `CountInv`, `BlockInv`, `NoEarlyExit`, `DoneOnExit`, `AllExitedAllDone` | PASS (3 workers, 3-4 vertices, blocks of 1 and 2) |
| | `Termination` liveness with stalls | PASS (blocks of 2; blocks of 1 in the long run) |
| | mutation: count the block after `nextChunk` | caught (`NoEarlyExit`) |
| `TerminationFlags` (8a9f1a9) | (a) `NoEarlyExit` | **FAIL**: realizable 24-state trace |
| | (b) `Termination` with 1 stalled idle worker | **FAIL**: stall-only configuration (no stale-bit route possible) |
| | (c) `Termination` with no stall | **FAIL**: stale bit from a failed root claim |
| | `AllExitedAllDone` | PASS: the early exit causes a hang or lost parallelism, not missing SCCs (in this abstraction) |
| `Suspension` (2a280d9 and b9d4ba4) | `NoDoubleResume`, `NoTwoRunners`, `OwnedUnblocked`, `FreeIdle`, `NoLostWakeup` | PASS (2 workers, 2 searches, 2 cells, lazy and embedded list, one 2-cell SCC or two 1-cell SCCs, recycling, transfers) |
| | `EventuallyResumed` (liveness) | see table (shrunk configuration) |
| | mutations: no re-check, blind resume, check-then-store, relaxed entry publication | all caught |
| | coverage: stale-entry CAS, two entries for one suspension, CAS race, null lazy list, transfer before re-check | all reachable |
| `CycleDetection` (2a280d9) | `NoDoubleRun`, `OneResolver`, `MembersFrozen`, `ResolverExclusive`, `NoStuckCycle` | PASS (N=2 with 2 cells each; N=3 with 1 cell each; N=3 with 2 cells each in the long run) |
| | `AllFinish` (liveness) | PASS (N=2, 2 cells; N=3, 1 cell) |
| | b9d4ba4 pass-1 walk (`B9d4Walk`) | see table |
| | mutations: no consensus CAS, walk before publishing, no second pass | all caught |
| | coverage: resolution, concurrent detection, odd age met, member blocked on the resolver's cell | all reachable |

## Full results

All counts come from the TLC output in `results/`. "TLC workers" is the `-workers` value of the run
that produced the file. The model was later capped at 2 workers because the machine is shared. Runs
recorded with 8 or 18 workers happened before that cap; they were not repeated if the result was
already in hand, since the worker count does not change what TLC explores. Times are wall-clock
seconds on an Apple M5 Max.

RESULTS_TABLE

**Long runs** completed earlier with 18 workers whose output files were later overwritten by the
short configuration of the same name. Re-run them with `LONG=1 ./run.sh`.

| Config | Result | Distinct states | Depth | Time |
| --- | --- | ---: | ---: | ---: |
| `Termination_stall_big_live` | PASS | 10,919,023 | 63 | 2,237 s (18 workers; mostly the single-threaded liveness phase) |
| `TerminationBlocks_c1_live` | PASS | 5,612,890 | 58 | 1,469 s (18 workers) |

`CycleDetection_n3_full` (30,409,862 states, PASS) is in the table above; its output is kept as
`results/CycleDetection_n3_full.out`. It ran before the `CellsPer` and `B9d4Walk` constants were
added, with the semantics they now have at `CellsPer = 2, B9d4Walk = FALSE`.
