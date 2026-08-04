# A simple 9p server

A smallish (<1MB) 9p server that can be static linked in busybox images.

Built for [qemount](https://github.com/bitplane/qemount)

Uses [libixp](https://github.com/0intro/libixp)

## Building

Run `make` to build a server with connected-stream and network transports.

For guests that only use an already-connected stream, such as a serial port,
build with `make NETWORK=0`.

## Usage

```text
simple9p [-d] [-r] [-p address] [directory]
```

Use `-r` to reject opens and operations that could modify the exported
filesystem. Writable service remains the default.

Without a dir, simple9p serves the platform's filesystem namespace: `/` on
Unix-like systems, or a startup snapshot of mounted volumes beneath a synthetic
`/` on Amiga-like systems. The namespace remains fixed for the life of the
server.

With network support enabled, the default address is `tcp!localhost!564`.
Use `-p -` for a bidirectional standard-input stream or `-p stream!path` for
an already-connected device such as a serial port.

## Status

This is slowly evolving into something that actually works. It's becoming more
robust, but it's still not tested enough to be deemed trustworthy - use at your
own risk.

## License

WTFPL with one additional clause:

* Don't blame me.

Do wtf you like, but you get what you pay for.
