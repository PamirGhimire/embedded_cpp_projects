#include "shared_ring.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <errno.h>

static std::string sanitize_sem(const std::string& base) {
    // ensure starts with '/'. Replace invalid characters with '_'
    std::string s = base;
    for (char &c : s) if (c=='/') c='_';
    return std::string("/") + s;
}

std::unique_ptr<SharedRing> SharedRing::create_or_open(const std::string& name,
                                                       uint32_t slot_count,
                                                       uint32_t slot_size, bool create) {
    auto p = std::unique_ptr<SharedRing>(new SharedRing());
    p->shm_name_ = name;
    p->slot_count_ = slot_count;
    p->slot_size_ = slot_size;
    p->owner_ = create;

    // derive sem names
    p->sem_free_name_ = sanitize_sem(name + "_free");
    p->sem_filled_name_ = sanitize_sem(name + "_filled");
    p->sem_mutex_name_ = sanitize_sem(name + "_mutex");

    if (!p->init_map(create)) return nullptr;

    // open/create semaphores
    if (create) {
        // create semaphores with initial values
        p->sem_free_ = sem_open(p->sem_free_name_.c_str(), O_CREAT | O_EXCL, 0666, slot_count);
        if (p->sem_free_ == SEM_FAILED) { perror("sem_open free"); return nullptr; }
        p->sem_filled_ = sem_open(p->sem_filled_name_.c_str(), O_CREAT | O_EXCL, 0666, 0);
        if (p->sem_filled_ == SEM_FAILED) { perror("sem_open filled"); return nullptr; }
        p->sem_mutex_ = sem_open(p->sem_mutex_name_.c_str(), O_CREAT | O_EXCL, 0666, 1);
        if (p->sem_mutex_ == SEM_FAILED) { perror("sem_open mutex"); return nullptr; }
    } else {
        p->sem_free_ = sem_open(p->sem_free_name_.c_str(), 0);
        if (p->sem_free_ == SEM_FAILED) { perror("sem_open free"); return nullptr; }
        p->sem_filled_ = sem_open(p->sem_filled_name_.c_str(), 0);
        if (p->sem_filled_ == SEM_FAILED) { perror("sem_open filled"); return nullptr; }
        p->sem_mutex_ = sem_open(p->sem_mutex_name_.c_str(), 0);
        if (p->sem_mutex_ == SEM_FAILED) { perror("sem_open mutex"); return nullptr; }
    }

    return p;
}

bool SharedRing::init_map(bool create) {
    // layout: header + slot_count * (4 + slot_size)
    size_t header_sz = sizeof(RingHeader);
    size_t single_slot = sizeof(uint32_t) + slot_size_;
    map_size_ = header_sz + (size_t)slot_count_ * single_slot;

    int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
    shm_fd_ = shm_open(shm_name_.c_str(), flags, 0666);
    if (shm_fd_ < 0) {
        perror("shm_open");
        return false;
    }

    if (create) {
        if (ftruncate(shm_fd_, (off_t)map_size_) != 0) {
            perror("ftruncate");
            close(shm_fd_);
            return false;
        }
    } else {
        // query size and verify at least header
        struct stat st;
        if (fstat(shm_fd_, &st) != 0) { perror("fstat"); close(shm_fd_); return false; }
        if ((size_t)st.st_size < header_sz) { std::cerr << "shm too small\n"; close(shm_fd_); return false; }
        // we don't enforce exact size here; we will map full shm file size
        map_size_ = st.st_size;
    }

    map_ptr_ = mmap(nullptr, map_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (map_ptr_ == MAP_FAILED) { perror("mmap"); close(shm_fd_); return false; }

    hdr_ = reinterpret_cast<RingHeader*>(map_ptr_);
    if (create) {
        hdr_->magic = 0xA1B2C3D4;
        hdr_->version = 1;
        hdr_->slot_count = slot_count_;
        hdr_->slot_size = slot_size_;
        hdr_->head = 0;
        hdr_->tail = 0;
        // zero memory for slots area
        size_t header_sz_local = sizeof(RingHeader);
        size_t slots_area = map_size_ - header_sz_local;
        std::memset(reinterpret_cast<char*>(map_ptr_) + header_sz_local, 0, slots_area);
    } else {
        if (hdr_->magic != 0xA1B2C3D4) { std::cerr << "bad shm magic\n"; return false; }
        // adopt existing counts
        slot_count_ = hdr_->slot_count;
        slot_size_ = hdr_->slot_size;
    }

    slots_start_ = reinterpret_cast<uint8_t*>(map_ptr_) + sizeof(RingHeader);
    return true;
}

SharedRing::~SharedRing() {
    if (sem_free_ != SEM_FAILED) sem_close(sem_free_);
    if (sem_filled_ != SEM_FAILED) sem_close(sem_filled_);
    if (sem_mutex_ != SEM_FAILED) sem_close(sem_mutex_);
    if (map_ptr_) munmap(map_ptr_, map_size_);
    if (shm_fd_ >= 0) close(shm_fd_);
}

void SharedRing::unlink_resources() {
    if (owner_) {
        sem_unlink(sem_free_name_.c_str());
        sem_unlink(sem_filled_name_.c_str());
        sem_unlink(sem_mutex_name_.c_str());
        shm_unlink(shm_name_.c_str());
    }
}

static int sem_wait_intr(sem_t* s) {
    int r;
    do { r = sem_wait(s); } while (r == -1 && errno == EINTR);
    return r;
}

bool SharedRing::write_message(const void* data, uint32_t len) {
    if (len > slot_size_) return false;
    if (!sem_wait_intr(sem_free_)) return false; // wait for free slot

    // lock header update
    sem_wait_intr(sem_mutex_);
    uint32_t idx = hdr_->tail;
    uint8_t* slot = slots_start_ + (size_t)idx * (sizeof(uint32_t) + slot_size_);
    // write length then data
    uint32_t l = len;
    std::memcpy(slot, &l, sizeof(uint32_t));
    std::memcpy(slot + sizeof(uint32_t), data, len);
    // update tail
    hdr_->tail = (idx + 1) % hdr_->slot_count;
    sem_post(sem_mutex_);

    // publish
    sem_post(sem_filled_);
    return true;
}

bool SharedRing::read_message(std::vector<uint8_t>& out) {
    if (!sem_wait_intr(sem_filled_)) return false;
    sem_wait_intr(sem_mutex_);
    uint32_t idx = hdr_->head;
    uint8_t* slot = slots_start_ + (size_t)idx * (sizeof(uint32_t) + slot_size_);
    uint32_t len;
    std::memcpy(&len, slot, sizeof(uint32_t));
    out.resize(len);
    std::memcpy(out.data(), slot + sizeof(uint32_t), len);
    hdr_->head = (idx + 1) % hdr_->slot_count;
    sem_post(sem_mutex_);

    sem_post(sem_free_);
    return true;
}
