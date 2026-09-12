#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* Global variables */
IxpServer server;
int debug = 0;
static int stream_read_fd;
static int stream_write_fd;
static IxpThread stream_thread;
static IxpThread *stream_base_thread;

/* 9P server operations */
Ixp9Srv p9srv = {
    .attach = fs_attach,
    .walk = fs_walk,
    .open = fs_open,
    .read = fs_read,
    .write = fs_write,
    .create = fs_create,
    .remove = fs_remove,
    .clunk = fs_clunk,
    .stat = fs_stat,
    .wstat = fs_wstat,
    .flush = fs_flush,
    .freefid = fs_freefid,
};

/*
 * A connected stream has exactly one possible source of input. Mark it ready
 * and let libixp's read block until a complete request arrives. This supports
 * streams, such as serial devices, which cannot be polled by the host OS.
 */
static int stream_select(int nfds, fd_set *readfds, fd_set *writefds,
                         fd_set *exceptfds, struct timeval *timeout) {
    (void)nfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout;

    if(readfds && FD_ISSET(stream_read_fd, readfds)) {
        FD_ZERO(readfds);
        FD_SET(stream_read_fd, readfds);
        return 1;
    }
    return 0;
}

static void stop_without_connection(IxpServer *srv) {
    if(!srv->conn)
        srv->running = 0;
}

/* Handle one already-connected stream directly. */
static ssize_t stream_write(int fd, const void *buffer, size_t count) {
    if(fd == stream_read_fd)
        fd = stream_write_fd;
    return stream_base_thread->write(fd, buffer, count);
}

static void serve_stream(int read_fd, int write_fd) {
    if(debug)
        fprintf(stderr, "serve_stream: Starting with read fd=%d, write fd=%d\n",
                read_fd, write_fd);

    stream_read_fd = read_fd;
    stream_write_fd = write_fd;
    stream_base_thread = ixp_thread;
    stream_thread = *ixp_thread;
    stream_thread.select = stream_select;
    stream_thread.write = stream_write;
    ixp_thread = &stream_thread;
    server.preselect = stop_without_connection;

    /* Set up 9P service on the already-connected fd */
    ixp_serve9conn_fd(&server, read_fd, &p9srv);

    if(debug)
        fprintf(stderr, "serve_stream: Entering server loop\n");

    /* Run the server event loop */
    ixp_serverloop(&server);

    if(debug)
        fprintf(stderr, "serve_stream: Connection closed\n");
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-d] [-h] [-r] [-p address] [directory]\n", prog);
    fprintf(stderr, "  -d          Enable debug output\n");
    fprintf(stderr, "  -h          Show this help\n");
    fprintf(stderr, "  -r          Serve the filesystem read-only\n");
    fprintf(stderr, "  -p address  Use '-' for a bidirectional stdin stream\n");
    fprintf(stderr, "              Use stream!path for a connected stream device\n");
    fprintf(stderr, "              Use streams!input!output for separate streams\n");
#ifndef NINED_NO_NETWORK
    fprintf(stderr, "              Otherwise listen on a libixp network address\n");
    fprintf(stderr, "              (default: tcp!localhost!564)\n");
#endif
    fprintf(stderr, "  directory   Root to serve (default: platform namespace)\n");
}

int main(int argc, char *argv[]) {
    char *addr = nil;
    int c;

    while((c = getopt(argc, argv, "dhrp:")) != -1) {
        switch(c) {
        case 'd':
            debug = 1;
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
        case 'r':
            nined.read_only = 1;
            break;
        case 'p':
            addr = optarg;
            break;
        default:
            usage(argv[0]);
            exit(1);
        }
    }

    if(optind + 1 < argc) {
        usage(argv[0]);
        exit(1);
    }

    if(namespace_init(optind < argc ? argv[optind] : NULL) < 0) {
        fprintf(stderr, "Cannot initialize served namespace: %s\n",
                strerror(errno));
        exit(1);
    }
    
    int fd;
    
    if(!addr) {
#ifdef NINED_NO_NETWORK
        usage(argv[0]);
        exit(1);
#else
        addr = "tcp!localhost!564";
#endif
    }
    
    if(debug)
        fprintf(stderr, "Starting 9P server on %s\n", addr);

    /* Check for stdio mode */
    if(strcmp(addr, "-") == 0) {
        /* Use stdin/stdout for 9P - requires bidirectional fd */
        /* Caller should use: 9d -p - /path <> /dev/device */
        fd = STDIN_FILENO;
        if(debug)
            fprintf(stderr, "Using stdio (fd %d) for 9P\n", fd);

        /* Initialize server structure */
        memset(&server, 0, sizeof(server));
        server.aux = &p9srv;

        /* Serve on stdin/stdout */
        serve_stream(fd, fd);

        nined_state_cleanup();
        namespace_cleanup();
        return 0;
    }

    /* Open an explicitly named connected stream. */
    if(strncmp(addr, "stream!", 7) == 0 && addr[7] != '\0') {
        fd = open(addr + 7, O_RDWR);
        if(fd < 0) {
            fprintf(stderr, "Failed to open stream %s: %s\n",
                    addr + 7, strerror(errno));
            exit(1);
        }
        if(debug)
            fprintf(stderr, "Opened stream %s as fd %d\n", addr + 7, fd);
        
        /* Initialize server structure */
        memset(&server, 0, sizeof(server));
        server.aux = &p9srv;
        
        /* Serve the connected stream directly */
        serve_stream(fd, fd);
    }
    else if(strncmp(addr, "streams!", 8) == 0 && addr[8] != '\0') {
        char *paths = strdup(addr + 8);
        char *output;
        int output_fd;

        if(!paths || !(output = strchr(paths, '!')) || output[1] == '\0') {
            fprintf(stderr, "Invalid split stream address: %s\n", addr);
            free(paths);
            exit(1);
        }
        *output++ = '\0';
        output_fd = open(output, O_WRONLY);
        if(output_fd < 0) {
            fprintf(stderr, "Failed to open output stream %s: %s\n",
                    output, strerror(errno));
            free(paths);
            exit(1);
        }
        fd = open(paths, O_RDONLY);
        if(fd < 0) {
            fprintf(stderr, "Failed to open input stream %s: %s\n",
                    paths, strerror(errno));
            close(output_fd);
            free(paths);
            exit(1);
        }
        memset(&server, 0, sizeof(server));
        server.aux = &p9srv;
        serve_stream(fd, output_fd);
        close(output_fd);
        free(paths);
    }
#ifndef NINED_NO_NETWORK
    else {
        /* Try as network address */
        fd = ixp_announce(addr);
        if(fd < 0) {
            fprintf(stderr, "Failed to announce on %s: %s\n", addr, strerror(errno));
            exit(1);
        }
        
        /* Initialize server */
        memset(&server, 0, sizeof(server));
        
        /* Start listening */
        ixp_listen(&server, fd, &p9srv, ixp_serve9conn, nil);
        
        /* Run server loop */
        ixp_serverloop(&server);
    }
#else
    else {
        fprintf(stderr, "Network transports are not available in this build\n");
        exit(1);
    }
#endif
    
    nined_state_cleanup();
    namespace_cleanup();
    return 0;
}
