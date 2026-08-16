# TODO

- Replace the global Qid mutation generation with a bounded per-path scheme
  without missing same-second, same-size writes.
- Verify chmod in the minimal Linux guest without `/proc`, then add the
  appropriate `fchmodat` fallback if required.
- Use `F_DUPFD_CLOEXEC` where the target platform provides it.
- Update Mountin to consume the complete 9d release archive, pass its
  embedded path bound, and remove the separate libixp source dependency.
