# A simple 9p server

A smallish (<1MB) 9p server that can be static linked in busybox images.

Built for [qemount](https://github.com/bitplane/qemount)

Uses [libixp](https://github.com/0intro/libixp)

## Building

Run `make deps`, then `make` to build a server with connected-stream and network
transports.

For guests that only use an already-connected stream, such as a serial port,
build with `make NETWORK=0`.

`make release` produces an optimized, stripped binary. Embedded builds can set
`S9_PATH_MAX` to reduce request stack usage; `make check-embedded` verifies the
1024-byte appliance profile.

## Usage

```text
simple9p [-d] [-r] [-p address] [directory]
```

Use `-r` for read only.

Without a dir, simple9p serves the platform's filesystem root: `/` on
Unix-like systems, or a path above the volumes elsewhere.

The default address for a network server is `tcp!localhost!564`.
Use `-p -` for stdio or `-p stream!path` for an existing device like a serial
port.

## Status

This is slowly evolving into something that actually works. It's becoming more
robust, but don't consider it trustworthy - use at your own risk.

## License

WTFPL with one additional clause:

* Don't blame me.

Do wtf you like, but you get what you pay for.
