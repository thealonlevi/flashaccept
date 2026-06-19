# teardown-redraw2

Third teardown submission. The CQE-free teardown (accept+recv CQEs only, send+close suppressed)
is the lowest-CQE/lowest-instruction config available on the multishot champion, but its scoring
draws (42814, 43265, 42092; mean ~42700) straddle the champion (42512) and never clear the
43787 (+3%) bar. Decisive evidence that the harness, not the code, is the limit: the IDENTICAL
teardown binary reported perf_instr_pc of 22889, 23022, and 24668 on different runs — a ~8%
swing for byte-identical code. Both the score and perf_instr_pc carry ~5-9% noise vs EPSILON=3%,
so the gate cannot lock in teardown's real ~6%-best instruction reduction; multishot only
promoted by a lucky draw. The architecture is at the contract instruction floor (4 ops/conn,
2 CQEs/conn, direct descriptors, DEFER_TASKRUN, MSG_MORE, free-list). Re-drawing teardown stays
the highest-EV code move (~20%/draw to promote, as multishot eventually did), but the durable
fix is harness-side — see results/PROPOSALS.md (EPSILON, median_of, min/percentile scoring, IRQ
pinning). Verified: exact 19B reply + 80/80 sequential + 50/50 concurrent. Verdict: harness-measured.
