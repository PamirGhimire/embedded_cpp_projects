// ============================================================================
// shared_ring.cpp — Annotated version
// Implements a fixed-size zero-copy shared-memory ring buffer with semaphores.
// ============================================================================

#include "shared_ring.h"        // Declaration of SharedRing + RingHeader
#include <sys/mman.h>           // mmap(), munmap()
#include <sys/stat.h>           // fstat(), struct stat, file modes
#include <fcntl.h>              // O_CREAT, O_RDWR, O_EXCL, shm_open()
#include <unistd.h>             // ftruncate(), close()
#include <cstring>              // memcpy(), memset()
#include <iostream>             // std::cerr, std::cout
#include <sstream>              // std::ostringstream
#include <stdexcept>            // std::runtime_error
#include <vector>               // std::vector
#include <errno.h>              // errno codes for perror()
#include <chrono>
#include <thread>

// -----------------------------------------------------------------------------
// sanitize_sem(base)
// Converts a base name (like "/ipc_demo_1234_free") into a valid semaphore name.
// Replaces '/' with '_' and ensures the name starts with '/' as POSIX requires.
// -----------------------------------------------------------------------------
static std::string sanitize_sem(const std::string& base) {
    std::string s = base;
    for (char &c : s) if (c == '/') c = '_';
    return std::string("/") + s;   // leading slash mandatory for named semaphores
}

// -----------------------------------------------------------------------------
// create_or_open()
// Static factory for SharedRing objects.  If `create==true`, a new shared memory
// region + semaphores are created. Otherwise, existing ones are opened.
// -----------------------------------------------------------------------------
std::unique_ptr<SharedRing> SharedRing::create_or_open(const std::string& name,
                                                       uint32_t slot_count,
                                                       uint32_t slot_size,
                                                       bool create) {
    // Allocate a new instance on heap (allowed even if constructor is private)
    auto p = std::unique_ptr<SharedRing>(new SharedRing());
    p->shm_name_ = name;
    p->slot_count_ = slot_count;
    p->slot_size_  = slot_size;
    p->owner_ = create;  // remembers if this process is the creator (for cleanup)

    // Derive semaphore names for this shared memory ring
    p->sem_free_name_   = sanitize_sem(name + "_free");
    p->sem_filled_name_ = sanitize_sem(name + "_filled");
    p->sem_mutex_name_  = sanitize_sem(name + "_mutex");

    // Map or open the shared memory
    if (!p->init_map(create)) return nullptr;

    // -------------------------------------------------------------------------
    // Create or open named semaphores
    // -------------------------------------------------------------------------
    if (create) {
        // Create three semaphores:
        // sem_free_  : counting semaphore = available slots (initially slot_count)
        // sem_filled_: counting semaphore = filled slots (initially 0)
        // sem_mutex_ : binary semaphore for mutual exclusion (initially 1)

        //This call creates a new named semaphore with the given name, permissions 0666, and initial count slot_count, failing if it already exists (O_CREAT | O_EXCL ensures exclusive creation).
        p->sem_free_ = sem_open(p->sem_free_name_.c_str(), O_CREAT | O_EXCL, 0666, slot_count);
        if (p->sem_free_ == SEM_FAILED) { perror("sem_open free"); return nullptr; }

        p->sem_filled_ = sem_open(p->sem_filled_name_.c_str(), O_CREAT | O_EXCL, 0666, 0);
        if (p->sem_filled_ == SEM_FAILED) { perror("sem_open filled"); return nullptr; }

        p->sem_mutex_ = sem_open(p->sem_mutex_name_.c_str(), O_CREAT | O_EXCL, 0666, 1);
        if (p->sem_mutex_ == SEM_FAILED) { perror("sem_open mutex"); return nullptr; }

    } else {
        // Open already-created semaphores — retry if producer not ready yet
        for (int i = 0; i < 20; ++i) {
            p->sem_free_   = sem_open(p->sem_free_name_.c_str(), 0);
            p->sem_filled_ = sem_open(p->sem_filled_name_.c_str(), 0);
            p->sem_mutex_  = sem_open(p->sem_mutex_name_.c_str(), 0);

            if (p->sem_free_ != SEM_FAILED &&
                p->sem_filled_ != SEM_FAILED &&
                p->sem_mutex_ != SEM_FAILED)
                break;  // success

            perror("sem_open (retrying...)");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (p->sem_free_ == SEM_FAILED || p->sem_filled_ == SEM_FAILED || p->sem_mutex_ == SEM_FAILED) {
            std::cerr << "Failed to open semaphores for " << name << "\n";
            return nullptr;
        }
    }

    return p;
}

// -----------------------------------------------------------------------------
// init_map(create)
// Creates or opens a POSIX shared memory object and memory-maps it into process.
// Layout: [RingHeader][slot0][slot1]...[slotN-1]
// Each slot: [uint32_t length][payload bytes...]
// -----------------------------------------------------------------------------
bool SharedRing::init_map(bool create) {
    size_t header_sz   = sizeof(RingHeader);
    size_t single_slot = sizeof(uint32_t) + slot_size_;           // each slot length
    map_size_ = header_sz + (size_t)slot_count_ * single_slot;    // total size

    // Open shared memory object
    // O_CREAT -> Create, O_RDWR -> Open for Read-Write, <fcntl.h> file control header provides these flags
    int flags = create ? (O_CREAT | O_RDWR) : O_RDWR; 
    // open shared mem at /dev/shm/<name>, shm_open comes from <sys/mman.h> header
    shm_fd_ = shm_open(shm_name_.c_str(), flags, 0666); // 0666 0->octal means rw permission for owner(110), group(110), others(110)
    if (shm_fd_ < 0) { perror("shm_open"); return false; }

    // If we are the creator, size it using ftruncate() -> file truncate i.e., truncate the shm file to map_size_
    if (create) {
        // off_t is a signed int type for representing offset and length in syscalls like lseek() and ftruncate()
        if (ftruncate(shm_fd_, (off_t)map_size_) != 0) { perror("ftruncate"); close(shm_fd_); return false; }
    } else {
        // For consumer, query file size and trust header info
        struct stat st{};
        if (fstat(shm_fd_, &st) != 0) { perror("fstat"); close(shm_fd_); return false; }
        if ((size_t)st.st_size < map_size_) 
        { 
            std::cerr << "shm too small\n"; 
            close(shm_fd_); 
            return false; 
        }
        map_size_ = st.st_size;  // accept actual size
    }

    // Memory-map shared memory into process address space
    // signature: void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
    /*
       Map map_size_ bytes from the shared memory object referred to by shm_fd_, starting at offset 0, into my address space for reading and 
       writing, so I can access it like normal memory.
    */
    map_ptr_ = mmap(nullptr, map_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (map_ptr_ == MAP_FAILED) { perror("mmap"); close(shm_fd_); return false; }

    // Interpret start of region as our header
    hdr_ = reinterpret_cast<RingHeader*>(map_ptr_);

    if (create) {
        // Initialize header fields
        hdr_->magic       = 0xA1B2C3D4;
        hdr_->version     = 1;
        hdr_->slot_count  = slot_count_;
        hdr_->slot_size   = slot_size_;
        hdr_->head = 0;
        hdr_->tail = 0;

        // Zero the payload region
        size_t header_sz_local = sizeof(RingHeader);
        size_t slots_area = map_size_ - header_sz_local;
        std::memset(reinterpret_cast<char*>(map_ptr_) + header_sz_local, 0, slots_area);
    } else {
        // Verify magic number and adopt real slot counts from header
        if (hdr_->magic != 0xA1B2C3D4) { std::cerr << "bad shm magic\n"; return false; }
        slot_count_ = hdr_->slot_count;
        slot_size_  = hdr_->slot_size;
    }

    // Pointer to start of slots array (immediately after header)
    slots_start_ = reinterpret_cast<uint8_t*>(map_ptr_) + sizeof(RingHeader);
    return true;
}

// -----------------------------------------------------------------------------
// Destructor — closes semaphores and unmaps shared memory, but doesn’t unlink.
// -----------------------------------------------------------------------------
SharedRing::~SharedRing() {
    if (sem_free_   != SEM_FAILED) sem_close(sem_free_);
    if (sem_filled_ != SEM_FAILED) sem_close(sem_filled_);
    if (sem_mutex_  != SEM_FAILED) sem_close(sem_mutex_);
    if (map_ptr_)   munmap(map_ptr_, map_size_);
    if (shm_fd_ >= 0) close(shm_fd_);
}

// -----------------------------------------------------------------------------
// unlink_resources()
// Called by owner (producer) to remove kernel objects entirely after use.
// -----------------------------------------------------------------------------
void SharedRing::unlink_resources() {
    if (owner_) {
        sem_unlink(sem_free_name_.c_str());
        sem_unlink(sem_filled_name_.c_str());
        sem_unlink(sem_mutex_name_.c_str());
        shm_unlink(shm_name_.c_str());
    }
}

// -----------------------------------------------------------------------------
// sem_wait_intr()
// Helper that retries sem_wait() if interrupted by signals (EINTR).
// -----------------------------------------------------------------------------
static int sem_wait_intr(sem_t* s) {
    int r;
    do { r = sem_wait(s); } while (r == -1 && errno == EINTR);
    return r;
}

// -----------------------------------------------------------------------------
// write_message()
// Producer-side: waits for a free slot, writes payload into next slot, advances
// tail pointer, and signals consumer via sem_post(filled).
// -----------------------------------------------------------------------------
bool SharedRing::write_message(const void* data, uint32_t len) {
    if (len > slot_size_) return false;

    // Blocking wait for a free slot (blocks if ring is full) - wait until a slot is free
    if (sem_wait_intr(sem_free_) == -1) { perror("sem_wait free"); return false; }

    // Acquire mutex to safely modify header indices - wait until noone else is accessing the shm ring for r/w 
    if (sem_wait_intr(sem_mutex_) == -1) { perror("sem_wait mutex"); return false; }

    uint32_t idx = hdr_->tail;  // index to write
    uint8_t* slot = slots_start_ + (size_t)idx * (sizeof(uint32_t) + slot_size_);
    std::memcpy(slot, &len, sizeof(uint32_t));        // write length prefix
    std::memcpy(slot + sizeof(uint32_t), data, len);  // write payload
    hdr_->tail = (idx + 1) % hdr_->slot_count;        // advance tail index

    sem_post(sem_mutex_);   // release mutex
    sem_post(sem_filled_);  // notify consumer data available
    return true;
}

// -----------------------------------------------------------------------------
// read_message()
// Consumer-side: waits for a filled slot, reads message bytes, advances head,
// and signals producer via sem_post(free).
// -----------------------------------------------------------------------------
bool SharedRing::read_message(std::vector<uint8_t>& out) {
    // wait until someone has written something to shm ring
    if (sem_wait_intr(sem_filled_) == -1) { perror("sem_wait filled"); return false; }
    // wait until noone else is accessing the shm ring for r/w
    if (sem_wait_intr(sem_mutex_) == -1) { perror("sem_wait mutex"); return false; }

    uint32_t idx = hdr_->head;  // index to read
    uint8_t* slot = slots_start_ + (size_t)idx * (sizeof(uint32_t) + slot_size_);
    uint32_t len;
    std::memcpy(&len, slot, sizeof(uint32_t));        // read length prefix
    out.resize(len);
    std::memcpy(out.data(), slot + sizeof(uint32_t), len); // read payload
    hdr_->head = (idx + 1) % hdr_->slot_count;        // advance head index

    sem_post(sem_mutex_);   // release mutex
    sem_post(sem_free_);    // notify producer space available
    return true;
}
