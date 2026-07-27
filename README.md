# A simple 9p server

A smallish (<1MB) 9p server that can be static linked in busybox images.

Built for [qemount](https://github.com/bitplane/qemount)

Uses [libixp](https://github.com/0intro/libixp)

## Usage

```text
simple9p [-d] [-p address] [directory]
```

An explicit directory serves that directory as before. Without one, simple9p
serves the platform's filesystem namespace: `/` on Unix-like systems, or a
startup snapshot of mounted volumes beneath a synthetic `/` on Amiga-like
systems. The namespace remains fixed for the life of the server.

## Status

This will eventually evolve into the default `qemount`'s back-end, unless I
find something better.

While 2000.U support now works and the tests pass, it still shouldn't be
considered trustworthy end to end. Expect weird edge cases and bugs that might
mangle files.

## License

WTFPL with one additional clause:

* Don't blame me.

Do whatever the fuck you want with it, but if it goes wrong it's on you.
