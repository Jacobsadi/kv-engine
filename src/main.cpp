#include <assert.h>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <vector>
#include <string>
#include<poll.h>
#include <map>
#include "buffer.h"
using namespace std;
struct Conn {
    int fd = -1;
    // application intention, for the event loop
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    // buffered input and output
    Buffer incoming;
    Buffer outgoing;
};
struct Response {
    uint32_t status = 0;
    vector<uint8_t> data;
};

enum {
    RES_OK  = 0,
    RES_ERR = 1,
    RES_NX  = 2,
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
    buf_init(&conn->incoming, 64);
    buf_init(&conn->outgoing, 64);
    return  conn;

}
static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out){
    if(cur + 4 > end){
        return false;
    }
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}
static bool read_str(const uint8_t *&cur, const uint8_t *end, size_t n, string &out){
    if(cur + n > end ){
        return false;
    }
    out.assign(cur, cur + n);
    cur += n;
    return true;
}
static uint32_t parse_req(const uint8_t *req, size_t size, vector<string> &cmd ){
    const uint8_t *end = req + size;
    uint32_t nstr = 0;
    if(!read_u32(req, end, nstr)){
        return -1;
    }
    if(nstr > k_max_msg){
        return -1;
    }
    while(cmd.size() < nstr){
        uint32_t len = 0;
        if(!read_u32(req, end, len)){            
            return -1;
        }
        cmd.push_back(string());
        if(!read_str(req, end, len, cmd.back())){
            return -1;
        }
    }
}
// placeholder; implemented later
static map<string, string> g_data;
static void do_request(vector<string> &cmd, Response &out){
    if(cmd.size() == 2 && cmd[0] == "get"){
        auto it = g_data.find(cmd[1]);
        if(it == g_data.end()){
            out.status = RES_NX; // not found 
            return;
        }
        const string &val = it->second;
        out.data.assign(val.begin(), val.end());

    }else if(cmd.size() == 3 && cmd[0] == "set"){
        g_data[cmd[1]].swap(cmd[2]);
    }else if(cmd.size() == 2 && cmd[0] == "del"){
        g_data.erase(cmd[1]);
    }else {
        out.status = RES_ERR; // unrecognized command
    }
}
static void make_response(const Response &resp, Buffer &out){
    uint32_t resp_len = 4 + (uint32_t)resp.data.size();
    buf_append(&out, (const uint8_t *)&resp_len, 4);
    buf_append(&out, (const uint8_t *)&resp.status, 4);
    buf_append(&out, resp.data.data(), resp.data.size());
}
static bool try_one_request(Conn *conn){
    if(buf_size(&conn->incoming) < 4){
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, conn->incoming.data_begin, 4);
    if(len > k_max_msg){
        msg("too long");
        conn->want_close = true;
        return false;
    }
    if(4 + len > buf_size(&conn->incoming)){
        return false;
    }
    const uint8_t *request = conn->incoming.data_begin + 4;
    fprintf(stderr, "client says: %.*s\n", (int)len, request);
    
    // handle a request
    // 1. Parse the command.
    // 2. Process the command and generate a response.
    // 3. Append the response to the output buffer.
    vector<string> cmd;
    if((parse_req(request, len, cmd) < 0)){
        conn->want_close = true;
        return false;
    }
    Response resp;
    do_request(cmd, resp);
    make_response(resp, conn->outgoing);
    buf_consume(&conn->incoming, 4 + len);

    return true;

}

static void handle_write(Conn *conn){
    assert(buf_size(&conn->outgoing) > 0);
    ssize_t rv = write(conn->fd, conn->outgoing.data_begin, buf_size(&conn->outgoing));
    if (rv < 0 && errno == EAGAIN) {
        return; // actually not ready
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;    // error handling
        return;
    }

    //remove written data 
    buf_consume(&conn->outgoing, (size_t)rv);
    if(buf_size(&conn->outgoing) == 0){
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
    if (rv < 0 && errno == EAGAIN) {
        return;
    }
    if(rv < 0){
        msg_errno("read() error");
        conn->want_close = true;
        return ;
    }
    buf_append(&conn->incoming, buf, (size_t)rv);
    while (try_one_request(conn)) {}

    if(buf_size(&conn->outgoing) > 0){
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
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        // the rest are connection sockets
        for (Conn *conn : fd2conn){
            if(!conn){
                continue;
            }
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            if(conn->want_read){
                pfd.events |= POLLIN;
            }
            if(conn->want_write){
                pfd.events |= POLLOUT;
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
            if(ready & POLLIN){
                assert(conn->want_read);
                handle_read(conn);
            }
            if(ready & POLLOUT){
                assert(conn->want_write);
                handle_write(conn);
            }
            if((ready & POLLERR) || conn->want_close){
                (void)close(conn->fd);
                fd2conn[conn->fd] = nullptr;
                buf_free(&conn->incoming);
                buf_free(&conn->outgoing);
                delete conn;

            }
        }
    }
    return 0;
}