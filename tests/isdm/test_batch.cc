#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"
#include "isdm/core/types.h"
#include "isdm/protocol/batch_data_message.h"

namespace isdm::test {
namespace {

using isdm::protocol::BatchDataMessage;
using isdm::protocol::kBatchDataHeaderSize;
using isdm::protocol::decode_batch_data;
using isdm::protocol::encode_batch_data;

float LoadFloat(DataView view) { return view.read_at<float>(0); }

}  // namespace

TEST(BatchTest, RoundTripsMultipleObjectsOfFixedType) {
    const std::uint32_t payload_size = sizeof(float);
    const ObjectId ids[] = {11, 22, 33};
    const float values[] = {1.0f, 2.5f, 3.25f};

    std::array<std::byte, 256> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_batch_data(ids, reinterpret_cast<const std::byte*>(values), 3, 100,
                                  0x7788ull, payload_size, writer));

    BatchDataMessage decoded;
    ASSERT_TRUE(decode_batch_data(DataView(storage.data(), writer.offset()), decoded));
    EXPECT_EQ(decoded.type_id, 100U);
    EXPECT_EQ(decoded.timestamp, 0x7788ull);
    EXPECT_EQ(decoded.count(), 3U);

    for (std::size_t i = 0; i < decoded.count(); ++i) {
        EXPECT_EQ(decoded.object_id(i), ids[i]);
        EXPECT_EQ(decoded.payload(i).size(), payload_size);
        EXPECT_FLOAT_EQ(LoadFloat(decoded.payload(i)), values[i]);
    }
}

TEST(BatchTest, PayloadsAreZeroCopyIntoFrame) {
    const std::uint32_t payload_size = sizeof(float);
    const ObjectId ids[] = {1, 2};
    const float values[] = {5.0f, 6.0f};

    std::array<std::byte, 128> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_batch_data(ids, reinterpret_cast<const std::byte*>(values), 2, 7, 0,
                                  payload_size, writer));

    BatchDataMessage decoded;
    ASSERT_TRUE(decode_batch_data(DataView(storage.data(), writer.offset()), decoded));
    EXPECT_EQ(decoded.payload(0).data(),
              static_cast<const std::byte*>(storage.data()) + kBatchDataHeaderSize +
                  8);  // first entry after header: oid(8) + payload
    EXPECT_EQ(decoded.entries.data(),
              static_cast<const std::byte*>(storage.data()) + kBatchDataHeaderSize);
}

TEST(BatchTest, SupportsEmptyBatch) {
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_batch_data(nullptr, nullptr, 0, 9, 0, sizeof(float), writer));

    BatchDataMessage decoded;
    ASSERT_TRUE(decode_batch_data(DataView(storage.data(), writer.offset()), decoded));
    EXPECT_EQ(decoded.type_id, 9U);
    EXPECT_EQ(decoded.count(), 0U);
}

TEST(BatchTest, RejectsCorruptMagic) {
    const ObjectId ids[] = {1};
    const float value = 1.0f;
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_batch_data(ids, reinterpret_cast<const std::byte*>(&value), 1, 5, 0,
                                  sizeof(float), writer));
    storage[0] = std::byte{0x00};

    BatchDataMessage decoded;
    EXPECT_FALSE(decode_batch_data(DataView(storage.data(), writer.offset()), decoded));
}

TEST(BatchTest, RejectsCountExceedingRemainingBytes) {
    // Hand-build a header claiming count = 0xFFFFFF but no entries.
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    writer.write<std::uint16_t>(isdm::protocol::kBatchDataMagic);
    writer.write<std::uint32_t>(100);       // type_id
    writer.write<std::uint64_t>(0);         // timestamp
    writer.write<std::uint32_t>(0xFFFFFu);  // count
    writer.write<std::uint32_t>(sizeof(float));  // payload_size

    BatchDataMessage decoded;
    EXPECT_FALSE(decode_batch_data(DataView(storage.data(), writer.offset()), decoded));
}

}  // namespace isdm::test