# conn free-list stacked on the direct-descriptor champion

Champion = 4d08ad9 = DEFER_TASKRUN + direct descriptors, 36585 (a high outlier of the
direct-desc distribution: draws 36585/36136/36048/27217). Stacked userspace levers keep
landing in the ~30% noise band below it: multishot 28684, shared-recv 33532 — both reverted.

iter19 hypothesis: replace per-conn calloc/free with a per-worker conn free-list
(conn_alloc/conn_free; single-threaded => no lock; bounded by peak concurrency ~tens).
Removes glibc malloc/free bookkeeping + calloc zeroing from the hot path (buf never read,
needn't be zeroed). Added `next` link to struct conn. io_uring path untouched. Keeps the
champion-level direct substrate while re-sampling the ~36k distribution with small upside.

TEST GOTCHA: 50 concurrent `nc -q1` showed 12/50 "0 bytes" — FALSE ALARM (nc -q1 racing
under many procs). A robust client (python, 600 conns, 64-way, full 19B read loop) =
600/600 correct. Lesson: don't trust mass-`nc -q1` for concurrency; use a real client to
verify the contract before suspecting a server bug.

~30% noise persists (results/PROPOSALS.md); 36585 is a high outlier, so clearing EPSILON
needs a high draw. If it reverts, userspace levers are exhausted — the harness is the limit.

iter20 (NEW scoring formula 1e9/instr_conn): re-applied free-list on top of f3fa203 champion
(28090, instr/conn=35599). New harness profile (run 38) shows libc_pct=1.32% vs old free-list
run's libc_pct=0.92% — confirming ~0.4% instr savings. Expected score: ~28203 (+0.4%).
Pool pre-populated with 128 conns (malloc at startup, not in hot path).
