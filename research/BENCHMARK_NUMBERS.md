## Benchmark (loopback, 1 core pinned, CPU-bound, fixed 512 in-flight, 3 reps, ~2-3% spread)
| server | instr/conn | conn/s (1 core) | vs Go | vs vanilla io_uring |
|---|---|---|---|---|
| Go goroutine-per-conn (riptide model) | 83,250 | 60,131 | 1.0x | - |
| vanilla io_uring (milestone-0) | 59,931 | 147,946 | 1.39x | 1.0x |
| flashaccept (optimized) | 27,363 | 361,282 | 3.04x | 2.19x |

flashaccept = 3.04x fewer CPU instructions/connection than Go (~6x conn/s per core),
              2.19x fewer than vanilla io_uring (~2.4x conn/s per core).
Techniques: multishot accept (direct), registered files/direct descriptors,
            per-worker connection freelist, MSG_MORE reply+FIN fusion, batched submit/harvest.
Measured: Intel Xeon Gold 6154 @ 3.0GHz, Linux 6.8, liburing 2.5, loopback (network-noise-free).
