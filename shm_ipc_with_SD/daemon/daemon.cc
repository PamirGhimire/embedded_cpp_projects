// Very small discovery daemon (UNIX datagram). Replies with PEER notifications.
// Protocol (ascii tokens):
//   REGISTER <service_key> <client_sock_path> <shm_name_or_->
//
// Server replies to registering client with existing peers as multiple:
//   PEER <service_key> <peer_sock> <peer_shm>
//
// And notifies existing peers of the new registration likewise.

#include <sys/socket.h>  // socket(), bind(), recvfrom(), sendto(), sockaddr, AF_UNIX, SOCK_DGRAM
#include <sys/un.h>      // sockaddr_un for UNIX domain sockets
#include <unistd.h>      // close(), unlink()
#include <iostream>      // std::cout, std::cerr
#include <string>        // std::string
#include <sstream>       // std::istringstream for parsing messages
#include <cstring>       // strncpy(), memset(), strlen()
#include <vector>        // std::vector
#include <map>           // std::map
#include <stdio.h>       // perror()

static constexpr std::string_view DAEMON_SOCK{"/tmp/ipc_daemon.sock"};  // Path of the UNIX socket file

struct ClientInfo { 
    std::string sock;   // Path to the client's socket
    std::string shm;    // Name of shared memory or "-"
};

int main() {
    unlink(DAEMON_SOCK.data());  // <unistd.h>: int unlink(const char* path); deletes old socket file if present

    // <sys/socket.h>: with the call, kernel allocates buffers and assigns a unique file descriptor for the socket object
    // AF_UNIX sockets don't use IP addresses - they're addressed by filesystem paths
    const int fd = socket(AF_UNIX, SOCK_DGRAM, 0);  

    if (fd < 0) { 
        perror("socket");  // <stdio.h>: print the last system error prefixed by "socket"
        return 1; 
    }

    sockaddr_un addr{};  // UNIX socket address structure, _un => unix domain, _in=> Internet domain
    addr.sun_family = AF_UNIX;  // Set address family to UNIX

    // strncpy(char * destination, char * source, size_t n); // Copy path safely into struct
    strncpy(addr.sun_path, DAEMON_SOCK.data(), sizeof(addr.sun_path) - 1);  

    // Bind socket to path, making it accessible to clients
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { 
        perror("bind");  // Print error if binding fails (e.g., permission or leftover file)
        close(fd);       // <unistd.h>: close the socket descriptor
        return 1; 
    }

    std::cout << "ipc_daemon listening at " << DAEMON_SOCK << "\n";

    std::map<std::string, std::vector<ClientInfo>> registry;  // service_key → list of registered clients

    while (true) {
        char buf[1024];  // Receive buffer for incoming datagrams
        sockaddr_un from{};  // Sender address
        socklen_t fromlen = sizeof(from);

        // <sys/socket.h>: recvfrom receives message and sender address - sender address is filled in &from
        ssize_t r = recvfrom(fd, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromlen);
        if (r <= 0) continue;  // Skip if error or empty packet

        buf[r] = '\0';  // Null-terminate message
        std::string msg(buf);  // Convert C string to std::string for parsing

        std::istringstream iss(msg);  // Tokenize message
        std::string cmd; 
        iss >> cmd;  // Extract first token (e.g., REGISTER)

        if (cmd == "REGISTER") {  // parse the rest of the message and handle client registration
            std::string key, client_sock, shm;
            iss >> key >> client_sock >> shm;  // Parse remaining tokens
            std::cout << "REGISTER: " << key << " " << client_sock << " " << shm << "\n";

            // client_sock is the client's transport layer address as a path in the local filesystem (similar to client's IP address in AF_INET)
            ClientInfo ci{client_sock, shm};
            auto &vec = registry[key];  // Create/get vector of clients for service key

            // Notify the new client about all existing peers
            for (const auto &p : vec) {
                std::string reply = "PEER " + key + " " + p.sock + " " + p.shm;
                sockaddr_un dest{};
                dest.sun_family = AF_UNIX;
                strncpy(dest.sun_path, client_sock.c_str(), sizeof(dest.sun_path) - 1);
                // <sys/socket.h>: send message to destination address
                sendto(fd, reply.c_str(), (int)reply.size(), 0, (sockaddr*)&dest, sizeof(dest));
            }

            vec.push_back(ci);  // Add new client to the list

            // Notify all existing peers about this new registration
            for (const auto &p : vec) {
                if (p.sock == client_sock) continue;  // Skip self
                std::string notify = "PEER " + key + " " + client_sock + " " + shm;
                sockaddr_un dest{};
                dest.sun_family = AF_UNIX;
                strncpy(dest.sun_path, p.sock.c_str(), sizeof(dest.sun_path) - 1);
                sendto(fd, notify.c_str(), (int)notify.size(), 0, (sockaddr*)&dest, sizeof(dest));
            }
        } else {
            std::cout << "unrecognised request: " << msg << "\n";  // Unknown command
        }
    }

    close(fd);           // <unistd.h>: close socket
    unlink(DAEMON_SOCK.data()); // <unistd.h>: remove socket file path after exit
    return 0;
}
