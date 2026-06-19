# linked reply chain REGRESSED (do not retry as-is)

Tried: queue recv->send->close as one IOSQE_IO_LINK chain with CQE_SKIP_SUCCESS
on recv/send (config 39bd0bf). It worked as designed on the syscall axis:
enter_pc dropped 1.896 -> 1.402 and the gate passed.

BUT score fell 29292 -> 19376 (~34%, well beyond noise) and it was reverted.

Lesson: on this box, minimizing io_uring_enter/conn is NOT the dominant lever.
Hard-linked chains serialize the recv->send->close ops and push them through the
slower/serialized completion path (recv on a not-yet-ready socket + linked deps
defeat inline fast-completion and add per-op link bookkeeping), costing more CPU
than the saved enters. Prefer levers that cut kernel CPU/completion (e.g.
[[defer-taskrun-single-issuer]]) over levers that only cut enter count.
