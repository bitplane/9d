ARG ARCH
FROM alpine:latest
ARG ARCH

# Install build dependencies
RUN apk add --no-cache \
    make \
    gcc \
    musl-dev \
    linux-headers

WORKDIR /build

# Copy our source files
COPY *.c *.h Makefile deps.mk ./
COPY libixp ./libixp

RUN make release

# serve
ENTRYPOINT ["/build/build/9d"]
