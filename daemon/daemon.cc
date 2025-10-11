// Very small discovery daemon (unix datagram). Replies with PEER notifications.
// Protocol (ascii tokens):
//   REGISTER <service_key> <client_sock_path> <shm_name_or_->
//
// Server replies to registering client with existing peers as multiple:
//   PEER <service_key> <peer_sock> <peer_shm>
//
// And notifies existing peers of the new registration likewise.

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <vector>
#include <map>

static const char* DAEMON_SOCK = "/tmp/ipc_daemon.sock";
struct ClientInfo { std::string sock; std::string shm; };

int main() {
    unlink(DAEMON_SOCK);
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DAEMON_SOCK, sizeof(addr.sun_path)-1);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(fd); return 1; }
    std::cout << "ipc_daemon listening at " << DAEMON_SOCK << "\n";

    std::map<std::string, std::vector<ClientInfo>> registry;

    while (true) {
        char buf[1024];
        sockaddr_un from{};
        socklen_t fromlen = sizeof(from);
        ssize_t r = recvfrom(fd, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);
        if (r <= 0) continue;
        buf[r] = '\0';
        std::string msg(buf);
        std::istringstream iss(msg);
        std::string cmd; iss >> cmd;
        if (cmd == "REGISTER") {
            std::string key, client_sock, shm;
            iss >> key >> client_sock >> shm;
            std::cout << "REGISTER: " << key << " " << client_sock << " " << shm << "\n";
            ClientInfo ci{client_sock, shm};
            // reply existing peers to the new client
            auto &vec = registry[key];
            for (const auto &p : vec) {
                std::string reply = "PEER " + key + " " + p.sock + " " + p.shm;
                // send to registering client (use client_sock path)
                sockaddr_un dest{}; dest.sun_family = AF_UNIX;
                strncpy(dest.sun_path, client_sock.c_str(), sizeof(dest.sun_path)-1);
                sendto(fd, reply.c_str(), (int)reply.size(), 0, (sockaddr*)&dest, sizeof(dest));
            }
            // add to registry
            vec.push_back(ci);
            // notify existing peers about the new one
            for (const auto &p : vec) {
                if (p.sock == client_sock) continue;
                std::string notify = "PEER " + key + " " + client_sock + " " + shm;
                sockaddr_un dest{}; dest.sun_family = AF_UNIX;
                strncpy(dest.sun_path, p.sock.c_str(), sizeof(dest.sun_path)-1);
                sendto(fd, notify.c_str(), (int)notify.size(), 0, (sockaddr*)&dest, sizeof(dest));
            }
        } else {
            std::cout << "unknown: " << msg << "\n";
        }
    }
    close(fd);
    unlink(DAEMON_SOCK);
    return 0;
}
