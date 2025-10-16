#include "ipc/shared_ring.h"
#include <gtest/gtest.h>

class SharedRingFixture : public ::testing::Test {
protected:
    std::unique_ptr<SharedRing> ring;
    void SetUp() override {
        ring = SharedRing::create_or_open("/test_ring", 4, 64, true);
    }
    void TearDown() override {
        if (ring) ring->unlink_resources();
    }
};


TEST_F(SharedRingFixture, WriteReadWorks) {
    std::string msg = "hello";
    EXPECT_TRUE(ring->write_message(msg.data(), msg.size()));
    std::vector<uint8_t> out;
    EXPECT_TRUE(ring->read_message(out));
    EXPECT_EQ(std::string(out.begin(), out.end()), msg);
}
