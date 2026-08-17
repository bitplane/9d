# TODO

- Replace the global Qid mutation generation with a bounded per-path scheme
  without missing same-second, same-size writes.
- Verify chmod in the minimal Linux guest without `/proc`, then add the
  appropriate `fchmodat` fallback if required.
