#include <array>
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

}  // namespace isdm::test