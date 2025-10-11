// Producer: creates shared ring, then registers with daemon and writes messages
#include "../ipc/shared_ring.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>

static const char* DAEMON_SOCK = "/tmp/ipc_daemon.sock";

static std::string make_client_sock_path() {
    pid_t pid = getpid();
    return std::string("/tmp/ipc_producer_") + std::to_string(pid) + ".sock";
}

static int send_register(const std::string& client_sock, const std::string& service_key, const std::string& shm_name) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    // bind our own socket to client_sock
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, client_sock.c_str(), sizeof(addr.sun_path)-1);
    unlink(client_sock.c_str());
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(fd); return -1; }
    // send REGISTER
    std::string msg = "REGISTER " + service_key + " " + client_sock + " " + shm_name;
    sockaddr_un dest{}; dest.sun_family = AF_UNIX; strncpy(dest.sun_path, DAEMON_SOCK, sizeof(dest.sun_path)-1);
    sendto(fd, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&dest, sizeof(dest));
    return fd; // keep fd to possibly receive replies (not used heavily here)
}

int main() {
    const std::string service = "demo";
    std::string shmname = std::string("/ipc_demo_") + std::to_string(getpid());
    const uint32_t SLOTS = 8;
    const uint32_t SLOT_SZ = 256;

    auto ring = SharedRing::create_or_open(shmname, SLOTS, SLOT_SZ, true);
    if (!ring) { std::cerr << "failed create ring\n"; return 1; }
    std::cout << "producer created shm: " << shmname << "\n";

    std::string client_sock = make_client_sock_path();
    int sock = send_register(client_sock, service, shmname);
    if (sock < 0) { std::cerr << "register failed\n"; return 1; }

    // simple writer loop
    for (int i=0;i<50;i++) {
        std::string s = std::string("Message ") + std::to_string(i);
        bool ok = ring->write_message(s.data(), (uint32_t)s.size());
        if (!ok) std::cerr << "write failed\n";
        std::cout << "WROTE: " << s << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "producer done\n";
    ring->unlink_resources(); // cleanup
    close(sock);
    unlink(client_sock.c_str());
    return 0;
}
