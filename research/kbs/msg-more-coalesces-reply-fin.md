# MSG_MORE coalesces reply+FIN into one TCP segment

Profile from winning free-list run (score=28680, instr_pc=33341):
- iowrite16: 4.07% — NIC doorbell (virtio TX ring kick), one per TCP segment transmitted
- virtqueue_add_split: 3.25% — virtio TX descriptor, one per segment

Currently two NIC TX operations per connection:
1. send(19 bytes) → kernel transmits immediately → iowrite16 #1
2. close_direct → tcp_send_fin() sends FIN segment → iowrite16 #2

With MSG_MORE on the send SQE:
- send(MSG_MORE) → kernel holds the 19-byte skb in the write queue (no TX yet)
- close_direct → tcp_send_fin() sees pending skb → sets FIN flag on it → ONE segment with
  [19 bytes + FIN] transmitted → iowrite16 #1 only (no separate FIN segment)

This halves the NIC doorbell and virtqueue overhead:
- iowrite16: 4.07% → ~2%
- virtqueue_add_split: 3.25% → ~1.6%
- Total: ~3.6% instruction reduction → expected score 28680 → ~29720

The reply bytes are still fully delivered before EOF (TCP data ordering guarantees that
the 19 bytes precede the FIN flag in the segment), so the contract is preserved.

Short-write re-sends also use MSG_MORE — the FIN is always piggybacked on the LAST send.
