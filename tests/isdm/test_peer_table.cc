#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "isdm/discovery/peer_table.h"
#include "isdm/protocol/discovery.h"

namespace isdm::test {
namespace {

PeerInfo make_peer(NodeId id = 42) {
    PeerInfo peer;
    peer.node_id = id;
    peer.node_name = "node-" + std::to_string(id);
    peer.endpoint = transport::Endpoint(192, 168, 1, 10, 9000);
    peer.capabilities = static_cast<std::uint32_t>(protocol::Capability::kUdp);
    peer.schema_fingerprints = {1, 2};
    return peer;
}

}  // namespace

TEST(PeerTableTest, AddsAndFindsPeer) {
    PeerTable table(std::chrono::milliseconds(3000));
    auto peer = make_peer();

    EXPECT_EQ(table.upsert(peer, std::chrono::milliseconds(100)), PeerEvent::kOnline);
    const auto* found = table.find(42);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->node_name, "node-42");
    EXPECT_TRUE(found->online);
    EXPECT_EQ(table.size(), 1U);
}

TEST(PeerTableTest, RefreshesExistingPeerWithoutOnlineEvent) {
    PeerTable table(std::chrono::milliseconds(3000));
    auto peer = make_peer();
    EXPECT_EQ(table.upsert(peer, std::chrono::milliseconds(100)), PeerEvent::kOnline);

    peer.node_name = "renamed";
    EXPECT_EQ(table.upsert(peer, std::chrono::milliseconds(200)), PeerEvent::kUpdated);
    EXPECT_EQ(table.find(42)->node_name, "renamed");
    EXPECT_EQ(table.size(), 1U);
}

TEST(PeerTableTest, ExpiresPeersAfterTimeout) {
    PeerTable table(std::chrono::milliseconds(100));
    auto peer = make_peer();
    ASSERT_EQ(table.upsert(peer, std::chrono::milliseconds(0)), PeerEvent::kOnline);

    std::vector<NodeId> offline;
    EXPECT_EQ(table.expire(std::chrono::milliseconds(99), &offline), 0U);
    EXPECT_EQ(table.expire(std::chrono::milliseconds(101), &offline), 1U);
    ASSERT_EQ(offline.size(), 1U);
    EXPECT_EQ(offline[0], 42U);
    EXPECT_NE(table.find(42), nullptr);
    EXPECT_FALSE(table.find(42)->online);
}

TEST(PeerTableTest, RefreshesOfflinePeerAsOnline) {
    PeerTable table(std::chrono::milliseconds(100));
    auto peer = make_peer();
    table.upsert(peer, std::chrono::milliseconds(0));
    table.expire(std::chrono::milliseconds(101));
    ASSERT_FALSE(table.find(42)->online);

    EXPECT_EQ(table.upsert(peer, std::chrono::milliseconds(150)), PeerEvent::kOnline);
    EXPECT_TRUE(table.find(42)->online);
}

TEST(PeerTableTest, RemovesPeer) {
    PeerTable table;
    table.upsert(make_peer(), std::chrono::milliseconds(0));
    EXPECT_TRUE(table.remove(42));
    EXPECT_FALSE(table.remove(42));
    EXPECT_EQ(table.find(42), nullptr);
    EXPECT_EQ(table.size(), 0U);
}

TEST(PeerTableTest, RejectsZeroNodeId) {
    PeerTable table;
    auto peer = make_peer(0);
    EXPECT_EQ(table.upsert(peer, std::chrono::milliseconds(0)), PeerEvent::kInvalid);
    EXPECT_EQ(table.size(), 0U);
}

}  // namespace isdm::test