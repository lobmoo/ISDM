#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "isdm/core/data_view.h"

namespace isdm::test {

TEST(DataViewTest, ReadsValuesZeroCopyFromBackingMemory) {
    std::uint32_t speed = 1500;
    float voltage = 380.0f;
    std::uint64_t timestamp = 42;

    std::byte storage[16];
    std::memcpy(storage, &speed, sizeof(speed));
    std::memcpy(storage + 4, &voltage, sizeof(voltage));
    std::memcpy(storage + 8, &timestamp, sizeof(timestamp));

    DataView view(storage, sizeof(storage));

    EXPECT_EQ(view.size(), 16U);
    EXPECT_EQ(view.read_at<std::uint32_t>(0), 1500U);
    EXPECT_FLOAT_EQ(view.read_at<float>(4), 380.0f);
    EXPECT_EQ(view.read_at<std::uint64_t>(8), 42U);
}

TEST(DataViewTest, ProvidesSubViewWithoutCopy) {
    std::byte storage[8];
    std::uint32_t value = 7;
    std::memcpy(storage + 4, &value, sizeof(value));

    DataView view(storage, 8);
    DataView sub = view.sub_view(4, 4);

    EXPECT_EQ(sub.size(), 4U);
    EXPECT_EQ(sub.read_at<std::uint32_t>(0), 7U);
}

TEST(DataViewTest, MutableViewWritesIntoBackingMemory) {
    std::byte storage[8] = {};

    MutableDataView view(storage, 8);
    view.write_at<std::uint32_t>(4, 99);

    std::uint32_t back = 0;
    std::memcpy(&back, storage + 4, sizeof(back));
    EXPECT_EQ(back, 99U);
}

TEST(DataViewTest, ViewDoesNotOwnItsMemory) {
    std::byte storage[4];
    DataView view(storage, 4);
    EXPECT_EQ(view.data(), static_cast<const std::byte*>(storage));
    MutableDataView mutable_view(storage, 4);
    EXPECT_EQ(mutable_view.data(), storage);
}

}  // namespace isdm::test