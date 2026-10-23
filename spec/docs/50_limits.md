## What these results do and do not establish

A PASS here means "no violation in any interleaving **within the stated bounds**". It is not a
proof for arbitrary numbers of workers, searches, cells or suspensions.

These models do **not** show:
1. **Tarjan correctness.** That the right SCCs come out is not modeled: no graph edges, ranks or the transfer's rank rule (C1).
2. **Unbounded parameters.** Every model uses at most 3 workers and a handful of searches, cells and suspensions.
3. **Large blocked lists.** `BlockedListIt` (8 or more entries) is not modeled; only `firstBuffer` is.
4. **Weaker memory orders.** The real code is checked under seq_cst only. `RelaxedEntry` is a one-directional approximation used as a mutation; no C++ memory-model checker was used.
5. **Pending-queue internals.** Pending is a set; moodycamel's internals are not modeled, only its documented spurious-empty `try_dequeue`. Liveness assumes that a repeatedly attempted dequeue of a non-empty queue eventually succeeds (strong fairness).
6. **Cell recycling.** b9d4ba4 recycled cells with generation ages; 2a280d9 does not, and only 2a280d9 is modeled. The cell-age reads and checks in b9d4ba4's walk are elided. Its extra pass-1 `getBlockingCell` re-read is modeled (`B9d4Walk`).
7. **Counter wrap-around.** Search ages are 32-bit at 2a280d9, so the ABA protection of the age CAS fails after 2^31 reuses. Ages are unbounded integers here.
8. **Exceptions and partial thread start.** The `aborted` path is not modeled.
9. **A machine-checked composition.** The termination models *assume* the hand-shake (Model 3) and cycle resolution (Model 4) work. The combination is argued by hand, not checked as one model.
