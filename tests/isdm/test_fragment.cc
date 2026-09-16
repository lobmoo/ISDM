#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/core/data_view.h"
#include "isdm/protocol/fragment.h"

namespace isdm::test {

TEST(FragmentTest, SplitsAndReassemblesLargeFrame) {
    std::vector<std::byte> original(5000);
    for (std::size_t i = 0; i < original.size(); ++i) {
        original[i] = static_cast<std::byte>(i & 0xFFU);
    }

    std::vector<std::vector<std::byte>> fragments;
    protocol::fragment_frame(42, DataView(original.data(), original.size()), 512,
                             [&](DataView datagram) {
                                 fragments.emplace_back(datagram.data(),
                                                         datagram.data() + datagram.size());
                             });

    ASSERT_GT(fragments.size(), 1U);
    protocol::FragmentAssembler assembler;
    std::vector<std::byte> restored;
    for (const auto& datagram : fragments) {
        if (assembler.feed(DataView(datagram.data(), datagram.size()), restored)) break;
    }

    ASSERT_EQ(restored.size(), original.size());
    EXPECT_EQ(restored, original);
}

TEST(FragmentTest, HandlesOutOfOrderFragments) {
    std::vector<std::byte> original(2048, std::byte{0xA5});
    std::vector<std::vector<std::byte>> fragments;
    protocol::fragment_frame(7, DataView(original.data(), original.size()), 256,
                             [&](DataView datagram) {
                                 fragments.emplace_back(datagram.data(),
                                                         datagram.data() + datagram.size());
                             });

    protocol::FragmentAssembler assembler;
    std::vector<std::byte> restored;
    for (auto it = fragments.rbegin(); it != fragments.rend(); ++it) {
        if (assembler.feed(DataView(it->data(), it->size()), restored)) break;
    }
    EXPECT_EQ(restored, original);
}

TEST(FragmentTest, IgnoresMalformedDatagrams) {
    protocol::FragmentAssembler assembler;
    std::vector<std::byte> restored;
    std::array<std::byte, 3> malformed{};
    EXPECT_FALSE(assembler.feed(DataView(malformed.data(), malformed.size()), restored));
}

TEST(FragmentTest, RejectsMissingFragment) {
    std::vector<std::byte> original(1024, std::byte{0x12});
    std::vector<std::vector<std::byte>> fragments;
    protocol::fragment_frame(11, DataView(original.data(), original.size()), 128,
                             [&](DataView datagram) {
                                 fragments.emplace_back(datagram.data(),
                                                         datagram.data() + datagram.size());
                             });

    protocol::FragmentAssembler assembler;
    std::vector<std::byte> restored;
    for (std::size_t i = 0; i + 1 < fragments.size(); ++i) {
        EXPECT_FALSE(assembler.feed(DataView(fragments[i].data(), fragments[i].size()),
                                    restored));
    }
    EXPECT_TRUE(restored.empty());
}

TEST(FragmentTest, ExpiresIncompleteGroups) {
    std::vector<std::byte> original(1024, std::byte{0x33});
    std::vector<std::vector<std::byte>> fragments;
    protocol::fragment_frame(21, DataView(original.data(), original.size()), 128,
                             [&](DataView datagram) {
                                 fragments.emplace_back(datagram.data(),
                                                         datagram.data() + datagram.size());
                             });

    protocol::FragmentAssembler assembler;
    std::vector<std::byte> restored;
    // Only feed the first fragment; the frame is incomplete.
    ASSERT_FALSE(assembler.feed(DataView(fragments[0].data(), fragments[0].size()), restored));
    ASSERT_EQ(assembler.pending_count(), 1U);

    // After a timeout it must be evicted.
    const auto later = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    assembler.expire(later, std::chrono::milliseconds(100));
    EXPECT_EQ(assembler.pending_count(), 0U);
}

TEST(FragmentTest, BoundsPendingGroups) {
    protocol::FragmentAssembler assembler;
    std::vector<std::byte> restored;
    // Feed many incomplete single-fragment frames; the assembler must not grow
    // without bound.
    std::array<std::byte, 20> datagram{};
    ByteWriter writer(MutableDataView(datagram.data(), datagram.size()));
    for (std::uint32_t i = 0; i < 10000; ++i) {
        ByteWriter w(MutableDataView(datagram.data(), datagram.size()));
        w.write<std::uint16_t>(isdm::protocol::kFragmentMagic);
        w.write<std::uint32_t>(i);          // unique frame_id
        w.write<std::uint16_t>(0);          // sequence
        w.write<std::uint16_t>(2);          // count (never completes)
        EXPECT_FALSE(assembler.feed(DataView(datagram.data(), datagram.size()), restored));
    }
    EXPECT_LE(assembler.pending_count(), protocol::FragmentAssembler::kMaxPending);
}

}  // namespace isdm::test