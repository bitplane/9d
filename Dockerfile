ARG ARCH
FROM alpine:latest
ARG ARCH

# Install build dependencies
RUN apk add --no-cache \
    git \
    make \
    gcc \
    musl-dev \
    linux-headers

WORKDIR /build

# Copy our source files
COPY *.c *.h Makefile ./

# Clone the tested libixp integration release
RUN git clone --depth 1 --branch qemount-0.2 https://github.com/bitplane/libixp.git && \
    make

# serve
ENTRYPOINT ["/build/build/simple9p"]
