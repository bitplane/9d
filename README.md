# A simple 9p server

A smallish (<1MB) 9p server that can be static linked in busybox images.

Built for [qemount](https://github.com/bitplane/qemount)

Uses [libixp](https://github.com/0intro/libixp)

## Building

Run `make` to build a server with connected-stream and network transports.
For guests that only use an already-connected stream, such as a serial port,
build with `make NETWORK=0`. This omits the network transport without changing
the resulting program name.

## Usage

```text
simple9p [-d] [-p address] [directory]
```

An explicit directory serves that directory as before. Without one, simple9p
serves the platform's filesystem namespace: `/` on Unix-like systems, or a
startup snapshot of mounted volumes beneath a synthetic `/` on Amiga-like
systems. The namespace remains fixed for the life of the server.

With network support enabled, the default address is `tcp!localhost!564`.
Use `-p -` for a bidirectional standard-input stream or `-p stream!path` for
an already-connected device such as a serial port.

## Status

This will eventually evolve into the default `qemount`'s back-end, unless I
find something better.

Fids retain their opened file, directory or symlink until clunk, so later path
renames and removals do not silently redirect I/O. POSIX exports anchor path
resolution at the original root directory and reject symlinks in intermediate
components. The direct protocol tests cover walk, mutation, metadata, directory
offset, symlink, containment and allocation-failure behaviour, but the server
is still young enough that important data should be served read-only or backed
up.

## License

WTFPL with one additional clause:

* Don't blame me.

Do whatever the fuck you want with it, but if it goes wrong it's on you.
