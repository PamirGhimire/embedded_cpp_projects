#include <chrono>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <array>
using namespace std;

static constexpr int SENTINELVALUE{std::numeric_limits<int>::min()};

class CircularBuffer
{
  public:
    static std::unique_ptr<CircularBuffer>& CreateOrRetrieve()
    {
      static std::unique_ptr<CircularBuffer> circBuffer{new CircularBuffer()};
      return circBuffer;
    }

    bool push(int newData)
    {
      std::lock_guard<std::mutex> lock(read_write_mutex);
      underlying_array_[write_index_] = newData;
      write_index_ = (1 + write_index_) % buffer_size_;
      return true;
    }

    // returns true if existing data can be popped, otherwise false
    // existingData is output param
    bool pop(int& existingData)
    {
      std::lock_guard<std::mutex> lock(read_write_mutex);
      if (underlying_array_[read_index_] != SENTINELVALUE)
      {
        existingData = underlying_array_[read_index_];
        underlying_array_[read_index_] = SENTINELVALUE;
        read_index_ = (1 + read_index_) % buffer_size_;

        return true;
      }
      return false;
    }
    
    void printBuffer() const
    {
      std::cout << std::endl;
      for (const int& entry: underlying_array_)
        std::cout << entry << " ";
      std::cout << std::endl;
    }

  private:
    CircularBuffer()
    {
      std::fill(underlying_array_.begin(), underlying_array_.end(), SENTINELVALUE);
    }

    static constexpr std::size_t buffer_size_{8};
    std::array<int, buffer_size_> underlying_array_{}; 
    std::size_t read_index_{0};
    std::size_t write_index_{0};
    std::mutex read_write_mutex;
};

void producer(const int period_in_ms)
{
  // writes something to the ring buffer once every period_in_ms milliseconds
  auto& buffer = CircularBuffer::CreateOrRetrieve();
  for (int i = 0; i < 16; ++i)
  {
    bool pushed = buffer->push(i);
    if (pushed)
      std::cout << "Producer :: pushed " << i << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(period_in_ms));
  }
}

void consumer(int period_in_ms)
{
  // reads something from the ring buffer once every period_in_ms milliseconds
  auto& buffer = CircularBuffer::CreateOrRetrieve();
  for (int i = 0; i < 16; ++i)
  {
    int newData;
    bool popped = buffer->pop(newData);
    if (popped)
      std::cout << "Consumer :: popped " << newData << std::endl;
    else
     std::cout << "Consumer :: could not pop" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(period_in_ms));
  }
}

// To execute C++, please define "int main()"
int main() {
  // one thread produces - writes
  const int producer_period_in_ms{100};
  std::thread producer_thread(producer, producer_period_in_ms);

  // one thread consumes - reads
  const int consumer_period_in_ms{50};
  std::thread consumer_thread(consumer, consumer_period_in_ms);

  producer_thread.join();
  consumer_thread.join();

  return 0;
}

