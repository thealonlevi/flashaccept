# DEFER_TASKRUN + SINGLE_ISSUER ring setup

Hypothesis: the champion's per-conn cost is dominated by kernel-side completion
overhead (task-work + cross-CPU IPI/eager wakeups), not by the number of
io_uring_enter calls. Reducing enters via SQE linking actually LOWERED the score
(see [[linked-reply-chain-regressed]]), so the lever is kernel CPU/completion.

Change: init each per-worker ring with
`IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN` (one pinned thread per
ring satisfies single-issuer; the loop already reaps via submit_and_wait, the
GETEVENTS enter that DEFER_TASKRUN batches task-work into). Ring init was moved
from main() into the worker thread so the creating task == the submitting task.
Falls back to a plain ring if the kernel rejects the flags. Data path unchanged
from the 29292 champion.

Expected: lower kernel CPU per completion -> higher conn/s-per-core at the same
enter_pc. Measured: TBD by harness.
