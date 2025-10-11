#pragma once
// Minimal POSIX shared-ring API for single-producer / single-consumer demo.
// Build with -std=c++17
#include <string>
#include <vector>
#include <semaphore.h>
#include <cstdint>
#include <memory>

struct RingHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t slot_count;
    uint32_t slot_size; // payload size (excluding 4-byte length)
    uint32_t head;
    uint32_t tail;
    uint8_t reserved[28]; // pad to 48/64 bytes if you like
};

class SharedRing {
public:
    // create = true -> create new shm and semaphores, otherwise open existing
    static std::unique_ptr<SharedRing> create_or_open(const std::string& name, uint32_t slot_count,
                                                      uint32_t slot_size, bool create);

    ~SharedRing();

    // producer: blocking write
    bool write_message(const void* data, uint32_t len);

    // consumer: blocking read into buffer (resized)
    bool read_message(std::vector<uint8_t>& out);

    std::string shm_name() const { return shm_name_; }

    // cleanup only if we created them
    void unlink_resources();

private:
    SharedRing() = default;
    bool init_map(bool create);

    std::string shm_name_;
    int shm_fd_ = -1;
    void* map_ptr_ = nullptr;
    size_t map_size_ = 0;
    RingHeader* hdr_ = nullptr;
    uint8_t* slots_start_ = nullptr;
    uint32_t slot_count_ = 0;
    uint32_t slot_size_ = 0; // payload
    bool owner_ = false;

    // semaphores (named)
    sem_t* sem_free_ = SEM_FAILED;
    sem_t* sem_filled_ = SEM_FAILED;
    sem_t* sem_mutex_ = SEM_FAILED;
    std::string sem_free_name_;
    std::string sem_filled_name_;
    std::string sem_mutex_name_;
};
