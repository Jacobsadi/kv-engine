#include <assert.h>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <vector>
#include<poll.h>

int val = 1;
struct Conn {
    int fd = -1;
    // application intention, for the event loop
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    // buffered input and output
    std::vector<uint8_t> incoming;
    std::vector<uint8_t> outgoing;
};


static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}
static void msg_errno(const char *msg) {
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}
const size_t k_max_msg = 4096;

static int32_t read_full(int fd, char *buf, size_t n){
    while(n > 0) {
        ssize_t rv = read(fd, buf, n);
        if(rv <= 0){
            return -1; // error or unexpected EOF 
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n){
    while(n > 0){    
        ssize_t rv = write(fd, buf, n);
        if(rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;


}

static int32_t one_request(int connfd){
    char rbuf[4 + k_max_msg];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if(err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    // body length
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);

    if(len > k_max_msg){
        msg("too long");
        return -1;
    }
    // request body 
    err = read_full(connfd, &rbuf[4], len);
    if(err){
        msg("read() error");
        return err;
    }
    // do something
    fprintf(stderr, "client says: %.*s\n", len, &rbuf[4]);

    // reply 
    const char reply[] = "recieved";
    char wbuf[4 + k_max_msg];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);

}
static void fd_set_nb(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}
// application callback when the listening socket is ready
static Conn *handle_accept(int fd) {
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0) {
        msg_errno("accept() error");
        return NULL;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port)
    );

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);

    // create a `struct Conn`
    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;
    return conn;
}
int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(0);
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) { die("bind()");}

    // listen 
    rv = listen(fd, SOMAXCONN);
    // a map of all cleint connections, keyed by fd 
    std::vector<Conn *> fd2conn;
    std::vector<struct pollfd> poll_args;
     
    while(true) {
        poll_args.clear();
        // put the listening sockets in the first position 
        struct pollfd pfd = {fd, POLL_IN, 0};
        poll_args.push_back(pfd);
        // the rest are connection sockets
        for (Conn *conn : fd2conn){
            if(!conn){
                continue;
            }
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            if(conn->want_read){
                pfd.events |= POLL_IN;
            }
            if(conn->want_write){
                pfd.events |= POLL_OUT;
            }
            poll_args.push_back(pfd);
        }

        // wait for readiness 
        // .data() is the pionter the first element of the array 
        int rv = poll(poll_args.data(), poll_args.size(), -1);
        if(rv < 0 && errno == EINTR){
            continue;
        }
        if(rv < 0){
            die("poll");
        }

        // handle the listening socket 
        if(poll_args[0].revents){
            if(Conn *conn = handle_accept(fd)){
                // put into the map 
                if(fd2conn.size() <= (size_t)conn->fd){
                    fd2conn.resize(conn->fd  +1);
                }
                assert(!fd2conn[conn->fd]);
                fd2conn[conn->fd] = conn;
            }
        }



        while(true){
            // here the server only serves one client connection at once      
            int32_t err = one_request(connfd); // it returns 1 on error 
            if(err){
                break;
            }
        }
        close(connfd);
    }
    return 0;
}