// ============================================================================
// producer.cc — Annotated version
// Demonstrates creating shared memory + semaphores and registering with daemon
// ============================================================================

#include "../ipc/shared_ring.h"      // our SharedRing abstraction for zero-copy ring buffer
#include <sys/socket.h>              // socket(), bind(), sendto(), sockaddr_un, AF_UNIX
#include <sys/un.h>                  // sockaddr_un structure for UNIX domain sockets
#include <unistd.h>                  // close(), getpid(), unlink()
#include <iostream>                  // std::cout, std::cerr
#include <chrono>                    // std::chrono::milliseconds
#include <thread>                    // std::this_thread::sleep_for
#include <cstring>                   // strncpy()

// -----------------------------------------------------------------------------
// Constant defining daemon's well-known UNIX socket path
// -----------------------------------------------------------------------------
static constexpr std::string_view DAEMON_SOCK = "/tmp/ipc_daemon.sock";

// -----------------------------------------------------------------------------
// Helper function: build a unique socket path for this producer process
// Uses PID to ensure uniqueness (e.g. /tmp/ipc_producer_<pid>.sock)
// -----------------------------------------------------------------------------
static std::string make_client_sock_path() {
    pid_t pid = getpid();                        // getpid() returns calling process ID
    return std::string("/tmp/ipc_producer_") + std::to_string(pid) + ".sock";
}

// -----------------------------------------------------------------------------
// Helper function: register this producer with the daemon
// 1. Creates a UNIX datagram socket (SOCK_DGRAM)
// 2. Binds it to a unique local path (client_sock)
// 3. Sends a REGISTER message to the daemon specifying service key + shm name
// Returns: socket file descriptor (so caller can stay bound to receive messages)
// -----------------------------------------------------------------------------
static int send_register(const std::string& client_sock,
                         const std::string& service_key,
                         const std::string& shm_name) {
    // Create a UNIX-domain datagram socket
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    // Prepare local socket address structure
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;                                           // address family
    strncpy(addr.sun_path, client_sock.c_str(), sizeof(addr.sun_path) - 1); // safe copy path
    unlink(client_sock.c_str());                                         // remove if exists

    // Bind this socket to the filesystem path, making it a receive endpoint
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }

    // Prepare REGISTER message for the daemon: "REGISTER <service> <sock> <shm>"
    std::string msg = "REGISTER " + service_key + " " + client_sock + " " + shm_name;

    // Prepare destination address (the daemon’s socket)
    sockaddr_un dest{};
    dest.sun_family = AF_UNIX;
    strncpy(dest.sun_path, DAEMON_SOCK.data(), sizeof(dest.sun_path) - 1);

    // Send message to daemon (no reply needed here)
    sendto(fd, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&dest, sizeof(dest));

    return fd;  // return open socket so producer can receive daemon replies later
}

// -----------------------------------------------------------------------------
// Main entry point — producer lifecycle
// -----------------------------------------------------------------------------
int main() {
    const std::string service = "demo";  // logical service key for discovery

    // -------------------------------------------------------------------------
    // Step 1: Create unique shared memory name (POSIX requires leading '/')
    // -------------------------------------------------------------------------
    std::string shmname = std::string("/ipc_demo_") + std::to_string(getpid());

    // Each message slot will hold up to 256 bytes; ring has 8 slots.
    const uint32_t SLOTS = 8;
    const uint32_t SLOT_SZ = 256;

    // -------------------------------------------------------------------------
    // Step 2: Create new shared memory ring and associated semaphores
    // -------------------------------------------------------------------------
    auto ring = SharedRing::create_or_open(shmname, SLOTS, SLOT_SZ, true);
    if (!ring) { std::cerr << "failed create ring\n"; return 1; }
    std::cout << "producer created shm: " << shmname << "\n";

    // -------------------------------------------------------------------------
    // Step 3: Register this producer with the discovery daemon
    // -------------------------------------------------------------------------
    std::string client_sock = make_client_sock_path();       // local socket path
    // create send socket, send 'register service' msg to ipc_daemon
    int sock = send_register(client_sock, service, shmname); 
    if (sock < 0) { std::cerr << "register failed\n"; return 1; }

    // -------------------------------------------------------------------------
    // Step 4: Producer main loop — allocate, write, and publish messages
    // -------------------------------------------------------------------------
    for (int i = 0; i < 120; i++) {
        // Compose text message payload (e.g., "Message 0")
        std::string s = std::string("Message ") + std::to_string(i);

        // Attempt to write message into shared memory ring
        // Returns false if semaphores not available or ring full
        bool ok = ring->write_message(s.data(), (uint32_t)s.size());

        if (!ok) std::cerr << "write failed\n";     // ring full or semaphore error
        else std::cout << "WROTE: " << s << "\n";   // confirmation log

        // Sleep 200ms between writes to simulate periodic publisher
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // -------------------------------------------------------------------------
    // Step 5: Cleanup phase — detach and unlink shared memory/semaphores
    // -------------------------------------------------------------------------
    std::cout << "producer done\n";
    ring->unlink_resources();     // unlink shm + semaphores if this process created them
    close(sock);                  // close UNIX socket descriptor
    unlink(client_sock.c_str());  // remove the socket file path from filesystem
    return 0;                     // exit cleanly
}
