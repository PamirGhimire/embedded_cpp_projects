#include <cstddef>
#include <iostream>
#include <mutex>
#include <stack>
#include <memory>
#include <thread>
#include <chrono>

class Packet {
public:
    std::string src{""};
    std::string dest{""};
};

template <typename T>
class ObjectPool {
public:
    static ObjectPool<T>* GetPool(std::size_t pool_size) {
        static ObjectPool<T>* object_pool = new ObjectPool<T>(pool_size);
        return object_pool;
    }

    // Acquire an object from the pool
    std::unique_ptr<T> acquire() {
        std::lock_guard<std::mutex> lock(pool_access_mutex_);
        if (num_objects_in_pool_ > 0) {
            num_objects_in_pool_--;
            // Get the top object and remove it from the stack
            auto obj = std::move(object_stack_.top());
            object_stack_.pop();
            return obj;
        }
        return nullptr;
    }

    // Release an object back to the pool
    bool release(std::unique_ptr<T> object) {
        std::lock_guard<std::mutex> lock(pool_access_mutex_);
        if (num_objects_in_pool_ < pool_size_) {
            object_stack_.push(std::move(object));
            num_objects_in_pool_++;
            return true;
        }
        return false;
    }

private:
    ObjectPool(std::size_t pool_size) : pool_size_(pool_size) {
        // Pre-allocate pool_size objects of type T
        for (std::size_t i = 0; i < pool_size_; ++i) {
            std::unique_ptr<T> new_object(new T());
            object_stack_.push(std::move(new_object));
            num_objects_in_pool_++;
        }
    }

    std::stack<std::unique_ptr<T>> object_stack_;
    std::size_t pool_size_{0};
    std::size_t num_objects_in_pool_{0};
    std::mutex pool_access_mutex_;
};

void UpdateSources() {
    auto packet_pool = ObjectPool<Packet>::GetPool(10);
    for (int i = 0; i < 10; ++i) {
        auto packet = packet_pool->acquire();
        if (packet) {
            packet->src = "SenderECU";
            bool released = packet_pool->release(std::move(packet));
            if (!released) {
                std::cout << "UpdateSources: could not return packet back to the pool\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void UpdateDestinations() {
    auto packet_pool = ObjectPool<Packet>::GetPool(10);
    for (int i = 0; i < 10; ++i) {
        auto packet = packet_pool->acquire();
        if (packet) {
            packet->dest = "ReceiverECU";
            bool released = packet_pool->release(std::move(packet));
            if (!released) {
                std::cout << "UpdateDestinations: could not return packet back to the pool\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    // Thread 1: acquire 10 packets and update their sources
    std::thread srcs_updater(UpdateSources);
    // Thread 2: acquire 10 packets and update their destinations
    std::thread dests_updater(UpdateDestinations);

    srcs_updater.join();
    dests_updater.join();
    return 0;
}
