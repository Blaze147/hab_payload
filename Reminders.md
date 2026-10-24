# Future work

- [ ] Revisit the 512-byte message limit. The current protocol validator and
  application message-size checks still limit messages to 512 bytes (at most
  three 240-byte payload fragments). Decide whether to expand support to as many
  as 256 fragments, matching the retransmit bitmap's addressable range and the
  statistics tracker's capacity. Statistics support alone does not lift the
  protocol limit. Before expanding it, choose a memory-bounded assembly/streaming
  strategy, update validation and tests, and measure SRAM and peak stack usage
  on the target. Current default firmware uses 1,979 of 2,560 bytes of static
  SRAM before stack or optional history storage.
