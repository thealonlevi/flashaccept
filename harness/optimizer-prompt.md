This is one optimization iteration for accept-bench. Goal: minimize CPU instructions per accepted
connection (score = 1e9/instr_per_conn). Work fast and thoroughly — token budget is NOT a concern.

1. **Spawn parallel subagents (Task tool, one message / multiple calls) to explore concurrently** —
   don't investigate serially. Suggested split: (a) mine ClickHouse `acceptbench.runs`/`profile` +
   `git log`/`kbs` for where instructions go now and what's ALREADY been tried (avoid repeats);
   (b) study the hot path in `treatment/`; (c) research concrete io_uring techniques to cut
   per-connection work (multishot accept, SQE linking, registered files/buffers, ring-mapped
   buffers, fixed reply buffer, batched submit/harvest). Use the clickhouse MCP tools for queries
   (champion: `SELECT config_hash,score FROM acceptbench.runs WHERE arm='treatment' AND gate_passed=1 ORDER BY score DESC LIMIT 1`).
2. Read `results/BEST.json` (current champion) and `kbs/INDEX.md`.
3. SYNTHESIZE the subagents' findings into ONE hypothesis: the single most promising change that
   reduces instructions/conn and hasn't been tried. Implement it by editing files under `treatment/` only.
4. Make sure it still builds (`make -C treatment`) and keeps the contract (exact 19-byte reply,
   one reply per connection then close, listens on `--port`). Do NOT run the load benchmark and do
   NOT commit — the harness does that.
5. Write/append a one-line lesson to `kbs/` (and `kbs/INDEX.md`) capturing the hypothesis.
6. Print 1-3 sentences: the hypothesis you implemented and the expected mechanism (this becomes the
   commit message). Then stop.

Leave your edits uncommitted in `treatment/`. The harness will commit, benchmark, score, and keep
or revert based on the score.
