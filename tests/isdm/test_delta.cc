#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "isdm/core/data_view.h"
#include "isdm/reflect.h"
#include "isdm/protocol/delta.h"

namespace delta_test {
struct Motor {
    std::uint32_t id;
    float speed;
    float current;
    float voltage;
};
}  // namespace delta_test

ISDM_STRUCT(delta_test::Motor, ISDM_FIELD(delta_test::Motor, id),
            ISDM_FIELD(delta_test::Motor, speed), ISDM_FIELD(delta_test::Motor, current),
            ISDM_FIELD(delta_test::Motor, voltage))

namespace isdm::test {
namespace {

using isdm::protocol::DeltaMessage;
using isdm::protocol::apply_delta;
using isdm::protocol::decode_delta;
using isdm::protocol::encode_delta;

const auto kDesc = [] {
    static const auto ref = isdm::describe<delta_test::Motor>(100, 1);
    return ref.descriptor();
}();

}  // namespace

TEST(DeltaTest, EncodesOnlyChangedFields) {
    delta_test::Motor old_value{1, 100.0f, 20.0f, 380.0f};
    delta_test::Motor new_value{1, 100.0f, 25.0f, 380.0f};  // only current changed

    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_delta(kDesc, 7, 100, 0x7788ull, &old_value, &new_value, writer));

    DeltaMessage decoded;
    ASSERT_TRUE(decode_delta(DataView(storage.data(), writer.offset()), decoded));
    EXPECT_EQ(decoded.object_id, 7U);
    EXPECT_EQ(decoded.type_id, 100U);
    EXPECT_EQ(decoded.timestamp, 0x7788ull);
    EXPECT_EQ(decoded.count(), 1U);
}

TEST(DeltaTest, ApplyPatchesBaseToNewValue) {
    delta_test::Motor old_value{1, 100.0f, 20.0f, 380.0f};
    delta_test::Motor new_value{1, 150.0f, 25.0f, 380.0f};  // speed + current changed

    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_delta(kDesc, 7, 100, 0, &old_value, &new_value, writer));

    DeltaMessage decoded;
    ASSERT_TRUE(decode_delta(DataView(storage.data(), writer.offset()), decoded));

    delta_test::Motor base = old_value;
    ASSERT_TRUE(apply_delta(kDesc, &base, decoded));
    EXPECT_EQ(base.id, new_value.id);
    EXPECT_EQ(base.speed, new_value.speed);
    EXPECT_EQ(base.current, new_value.current);
    EXPECT_EQ(base.voltage, new_value.voltage);
}

TEST(DeltaTest, NoChangeReturnsFalse) {
    delta_test::Motor value{1, 100.0f, 20.0f, 380.0f};
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    EXPECT_FALSE(encode_delta(kDesc, 7, 100, 0, &value, &value, writer));
}

TEST(DeltaTest, RejectsCorruptMagic) {
    delta_test::Motor old_value{1, 100.0f, 20.0f, 380.0f};
    delta_test::Motor new_value{1, 150.0f, 20.0f, 380.0f};
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_delta(kDesc, 7, 100, 0, &old_value, &new_value, writer));
    storage[0] = std::byte{0x00};

    DeltaMessage decoded;
    EXPECT_FALSE(decode_delta(DataView(storage.data(), writer.offset()), decoded));
}

TEST(DeltaTest, RejectsUnknownFieldId) {
    delta_test::Motor old_value{1, 100.0f, 20.0f, 380.0f};
    delta_test::Motor new_value{1, 100.0f, 25.0f, 380.0f};
    std::array<std::byte, 64> storage{};
    ByteWriter writer(MutableDataView(storage.data(), storage.size()));
    ASSERT_TRUE(encode_delta(kDesc, 7, 100, 0, &old_value, &new_value, writer));

    DeltaMessage decoded;
    ASSERT_TRUE(decode_delta(DataView(storage.data(), writer.offset()), decoded));

    // Rewrite the entry's field_id to something unknown.
    const std::size_t entries_offset = 26;  // kDeltaHeaderSize
    auto* p = reinterpret_cast<std::uint32_t*>(storage.data() + entries_offset);
    *p = 99;

    delta_test::Motor base = old_value;
    EXPECT_FALSE(apply_delta(kDesc, &base, decoded));
}

}  // namespace isdm::test