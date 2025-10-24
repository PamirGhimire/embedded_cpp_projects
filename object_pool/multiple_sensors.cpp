#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>
#include <chrono>
#include <iomanip>

// =============================
// SensorSample + CircularBuffer
// =============================
uint64_t get_ms_timestamp()
{
    static uint64_t ms_timestamp{0};
    return ms_timestamp++;

}

struct SensorSample {
    std::string sensor_name;
    double value;
    uint64_t timestamp_ms;
};

class CircularSensorBuffer {
public:
    explicit CircularSensorBuffer(size_t capacity)
        : capacity_(capacity), buffer_(capacity) {}

    void push(const SensorSample& s)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        buffer_[head_] = s;
        head_ = (head_ + 1) % capacity_;
        if (size_ < capacity_)
            size_++;
    };
    std::vector<SensorSample> get_all() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<SensorSample> samples_to_return(buffer_.begin(), buffer_.begin() + size_);
        return samples_to_return;
    };

private:
    size_t capacity_;
    std::vector<SensorSample> buffer_;
    size_t head_{0};
    size_t size_{0};
    mutable std::mutex mtx_;
};

// =============================
// Abstract Sensor Base
// =============================
class Sensor {
public:
    explicit Sensor(std::string name,
                    std::shared_ptr<CircularSensorBuffer> buf)
        : name_(std::move(name)), buffer_(std::move(buf)) {}

    virtual ~Sensor() = default;
    virtual void run() = 0; // each sensor thread executes this
    virtual std::string get_name() {return name_;};

protected:
    virtual double generate_sample() = 0;
    std::string name_;
    std::shared_ptr<CircularSensorBuffer> buffer_;
};

// =============================
// Derived Sensors
// =============================
class WheelSpeedSensor : public Sensor {
public:
    using Sensor::Sensor;
    void run() override
    {
        while (true)
        {
            double new_sample = generate_sample();
            buffer_->push({name_, new_sample, get_ms_timestamp()});
            const int period_in_ms = 500;
            std::this_thread::sleep_for(std::chrono::milliseconds(period_in_ms));
        }
    };
protected:
    double generate_sample() override
    {
        static double sample{10};
        sample = (sample + 0.01);
        return sample;
    };
};

class YawRateSensor : public Sensor {
public:
    using Sensor::Sensor;
    void run() override
    {
        while (true)
        {
            double new_sample = generate_sample();
            buffer_->push({name_, new_sample, get_ms_timestamp()});
            const int period_in_ms = 1000;
            std::this_thread::sleep_for(std::chrono::milliseconds(period_in_ms));
        }
    };
protected:
    double generate_sample() override
    {
        static double sample{0};
        sample += 0.001;
        return sample;
    };
};

// =============================
// SensorManager
// =============================
class SensorManager {
public:
    explicit SensorManager(size_t buffer_capacity)
        : buffer_(std::make_shared<CircularSensorBuffer>(buffer_capacity)) {}

    std::shared_ptr<CircularSensorBuffer> get_buffer()
    {
        return buffer_;
    }

    void add_sensor(std::unique_ptr<Sensor> s)
    {
        sensors_.emplace_back(std::move(s));
    };
    void start_all()
    {
        for (auto& sensor : sensors_)
        {
            std::cout << sensor->get_name() << std::endl;
            threads_.emplace_back([&sensor](){sensor->run();});
        }
    };
    void join_all()
    {
        for (auto& thread: threads_)
            thread.join();
    };

    void print_all() const
    {
        std::vector<SensorSample> all_samples = buffer_->get_all();
        for (const auto& sample : all_samples)
            std::cout << "[" << sample.timestamp_ms << "] " << sample.sensor_name << " sensor -> " << sample.value << std::endl;
    };

private:
    std::shared_ptr<CircularSensorBuffer> buffer_;
    std::vector<std::unique_ptr<Sensor>> sensors_;
    std::vector<std::thread> threads_;
};

// =============================
// Main Test Harness
// =============================
int main() {
    SensorManager manager(10);
    manager.add_sensor(std::make_unique<WheelSpeedSensor>("WheelSpeed", manager.get_buffer())); // fix in implementation
    manager.add_sensor(std::make_unique<YawRateSensor>("YawRate", manager.get_buffer()));       // fix in implementation
    manager.start_all();

    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        manager.print_all();
    }

    manager.join_all();
    return 0;
}
