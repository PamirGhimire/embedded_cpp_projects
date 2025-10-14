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
#include <signal.h>
#include <atomic>

std::atomic<bool> terminate{false};
static void on_signal(int)
{
    terminate = true;
}

// -----------------------------------------------------------------------------
// Register signal handler
// -----------------------------------------------------------------------------
struct SigInit {                              // Define a small helper struct
    SigInit() {                               // Constructor runs automatically when instantiated
        struct sigaction sa{};                // POSIX struct describing how to handle a signal
        sa.sa_handler = on_signal;            // Assign our handler function for incoming signals
        sigemptyset(&sa.sa_mask);             // Initialize mask to empty (no signals blocked during handler)
        sigaction(SIGINT, &sa, nullptr);      // Register handler for Ctrl+C (interrupt)
        sigaction(SIGTERM, &sa, nullptr);     // Register handler for termination signal
    }
} _siginit;                                   // Create one global instance; constructor runs at startup



// -----------------------------------------------------------------------------
// Constant defining daemon's well-known UNIX socket path
// -----------------------------------------------------------------------------
static constexpr std::string_view DAEMON_SOCK = "/tmp/ipc_daemon.sock";


class SocketToDaemon
{
public:
    SocketToDaemon()
    {
        // Intialize self's UNIX-domain datagram socket
        fd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd_ < 0) { 
            perror("socket");
            throw std::runtime_error("could not create producer socket"); 
        }

        // Prepare local socket address structure
        client_sock_ = std::string("/tmp/ipc_producer_") + std::to_string(getpid()) + ".sock";
        addr_self_.sun_family = AF_UNIX;                                           // address family
        strncpy(addr_self_.sun_path, client_sock_.c_str(), sizeof(addr_self_.sun_path) - 1); // safe copy path
        unlink(client_sock_.c_str());                                         // remove if exists

        // Bind this socket to the filesystem path, making it a receive endpoint
        if (bind(fd_, (sockaddr*)&addr_self_, sizeof(addr_self_)) < 0) {
            perror("bind"); 
            close(fd_); 
            throw std::runtime_error("could not bind producer socket to path " + client_sock_);
        }
    
        // Initialize path to the daemon’s socket
        addr_daemon_.sun_family = AF_UNIX;
        strncpy(addr_daemon_.sun_path, DAEMON_SOCK.data(), sizeof(addr_daemon_.sun_path) - 1);    
    }

    void Register(const std::string& service_name, const std::string& shm_name)
    {
        // Prepare REGISTER message for the daemon: "REGISTER <service> <sock> <shm>"
        std::string msg = "REGISTER " + service_name + " " + client_sock_ + " " + shm_name;
        // Send message to daemon (no reply needed here)
        sendto(fd_, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&addr_daemon_, sizeof(addr_daemon_));
    }

    void Deregister(const std::string& service_name, const std::string& shm_name)
    {
        // Prepare REGISTER message for the daemon: "REGISTER <service> <sock> <shm>"
        std::string msg = "DEREGISTER " + service_name + " " + client_sock_ + " " + shm_name;
        // Send message to daemon (no reply needed here)
        sendto(fd_, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&addr_daemon_, sizeof(addr_daemon_));
    }

    ~SocketToDaemon()
    {
        close(fd_);                  // close UNIX socket descriptor
        unlink(client_sock_.c_str());  // remove the socket file path from filesystem
    }

private:
    int fd_{};
    sockaddr_un addr_self_{}; //address of producer
    sockaddr_un addr_daemon_{};
    std::string client_sock_{};
};



// -----------------------------------------------------------------------------
// Main entry point — producer lifecycle
// -----------------------------------------------------------------------------
int main() {
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
    SocketToDaemon connection_to_daemon;
    connection_to_daemon.Register("demo", shmname);    

    // -------------------------------------------------------------------------
    // Step 4: Producer main loop — allocate, write, and publish messages
    // -------------------------------------------------------------------------
    for (int i = 0; (i < 120 && (!terminate)); i++) {
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
    // deregister
    connection_to_daemon.Deregister("demo", shmname);  

    // wait for sometime so that consumers can react and stop accessing shm
    // todo
    
    // cleanup resources
    ring->unlink_resources();     // unlink shm + semaphores if this process created them
    return 0;                     // exit cleanly
}
