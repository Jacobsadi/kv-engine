#include <assert.h>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <netinet/in.h>
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

static void fd_set_nb(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}
// application callback when the listening socket is ready
static Conn *handle_accept(int fd) {
    // accept
    struct sockaddr_in client_addr {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if(connfd < 0){
        msg_errno("accept() error");
        return nullptr;
    }
    // set a new connection fd nonblocking mode 
    fd_set_nb(connfd);
    // create a struct Conn
    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true; // read the first request 
    return  conn;

}
// append to the back of the vector 
static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len){
    buf.insert(buf.end(), data, data + len);
}
static void buf_consume(std::vector<uint8_t> &buf, size_t n){
    buf.erase(buf.begin(), buf.begin() + n);
}
static bool try_one_request(Conn *conn){
    if(conn->incoming.size() < 4){
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if(len > k_max_msg){
        msg("too long");
        conn->want_close = true;
        return false;
    }
    if(4 + len > conn->incoming.size()){
        return false;
    }
    const uint8_t *request = &conn->incoming[4];
    fprintf(stderr, "client says: %.*s\n", (int)len, request);
    buf_append(conn->outgoing, (const uint8_t *)&len, 4);
    buf_append(conn->outgoing, request, len);

    buf_consume(conn->incoming, 4 + len);

    return true;

}

static void handle_write(Conn *conn){
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) {
        return; // actually not ready
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;    // error handling
        return;
    }

    //remove written data 
    buf_consume(conn->outgoing, (size_t)rv);
    if(conn->outgoing.size() == 0){
        conn->want_read = true;
        conn->want_write = false;
    }
}

static void handle_read(Conn *conn){
    // Don nonblocking read 
    uint8_t buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv == 0) {
        conn->want_close = true;
        return;
    }
    if(rv < 0){
        msg_errno("read() error");
        conn->want_close = true;
        return ;
    }
    buf_append(conn->incoming, buf, (size_t)rv);
    while (try_one_request(conn)) {}

    if(conn->outgoing.size() > 0){
        conn->want_read = false;
        conn->want_write = true;
    }
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

    // set to nonblocking 
    fd_set_nb(fd);
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

        // handle connection sockets 
        for(size_t i=1; i < poll_args.size(); i++){
            uint32_t ready = poll_args[i].revents;
            if(ready == 0){
                continue;
            }

            Conn *conn = fd2conn[poll_args[i].fd];
            if(ready & POLL_IN){
                assert(conn->want_read);
                handle_read(conn);
                if(conn->outgoing.size() > 0 && conn->want_write){
                    handle_write(conn);
                }
            }
            if(ready & POLL_OUT){
                assert(conn->want_write);
                handle_write(conn);
            }
            if((ready & POLLERR) || conn->want_close){
                (void)close(conn->fd);
                fd2conn[conn->fd] = nullptr;
                delete conn;

            }
        }
    }
    return 0;
}