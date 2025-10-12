// Consumer: registers, receives PEER messages from daemon and opens shm to consume
#include "../ipc/shared_ring.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>

static const char* DAEMON_SOCK = "/tmp/ipc_daemon.sock";

static std::string make_client_sock_path() {
    pid_t pid = getpid();
    return std::string("/tmp/ipc_consumer_") + std::to_string(pid) + ".sock";
}

int main() {
    const std::string service = "demo";
    std::string client_sock = make_client_sock_path();

    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, client_sock.c_str(), sizeof(addr.sun_path)-1);
    unlink(client_sock.c_str());
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(fd); return 1; }

    // send REGISTER with no shm
    std::string reg = "REGISTER " + service + " " + client_sock + " -";
    sockaddr_un dest{}; dest.sun_family = AF_UNIX;
    strncpy(dest.sun_path, DAEMON_SOCK, sizeof(dest.sun_path)-1);
    sendto(fd, reg.c_str(), (int)reg.size(), 0, (sockaddr*)&dest, sizeof(dest));

    std::cout << "consumer registered; waiting for PEER messages\n";

    SharedRing* ring = nullptr;
    std::unique_ptr<SharedRing> ring_ptr;

    while (true) {
        char buf[1024];
        sockaddr_un from{};
        socklen_t fromlen = sizeof(from);
        ssize_t r = recvfrom(fd, buf, sizeof(buf)-1, 0, (sockaddr*)&from, &fromlen);
        if (r <= 0) continue;
        buf[r] = '\0';
        std::string msg(buf);
        std::cout << "daemon-> " << msg << "\n";
        std::istringstream iss(msg);
        std::string cmd; iss >> cmd;
        if (cmd == "PEER") {
            std::string key, peer_sock, peer_shm;
            iss >> key >> peer_sock >> peer_shm;
            if (peer_shm != "-") {
                // open the shm ring
                std::cout << "opening shared ring: " << peer_shm << "\n";
                for (int i = 0; i < 20; ++i) 
                {
                    ring_ptr = SharedRing::create_or_open(peer_shm, 1, 1, false);
                    if (ring_ptr) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                if (!ring_ptr) {
                    std::cerr << "Failed to open shared ring after retries\n";
                    continue;
                }

                // consume until exhausted (read the slot at RingHeader->head)
                std::vector<uint8_t> b;
                while (ring_ptr->read_message(b)) {
                    std::string s(b.begin(), b.end());
                    std::cout << "READ: " << s << "\n";
                }
                // keep receiving notifications; for demo, exit after reading some?
            }
        }
    }

    close(fd);
    unlink(client_sock.c_str());
    return 0;
}
