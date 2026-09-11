#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "isdm/transport/endpoint.h"

namespace isdm::test {

using isdm::transport::Endpoint;

TEST(EndpointTest, ConstructsFromOctetsAndPort) {
    Endpoint ep(192, 168, 1, 10, 5000);
    EXPECT_EQ(ep.port(), 5000U);
    EXPECT_EQ(ep.to_string(), "192.168.1.10:5000");
}

TEST(EndpointTest, ParsesIPv4AndPort) {
    Endpoint ep;
    ASSERT_TRUE(ep.parse("10.0.0.1:9990"));
    EXPECT_EQ(ep.to_string(), "10.0.0.1:9990");
}

TEST(EndpointTest, RejectsMalformedInput) {
    Endpoint ep;
    EXPECT_FALSE(ep.parse("not-an-address"));
    EXPECT_FALSE(ep.parse("1.2.3.4"));       // no port
    EXPECT_FALSE(ep.parse("1.2.3.4:70000"));  // port out of range
    EXPECT_FALSE(ep.parse("999.1.1.1:80"));   // octet out of range
}

TEST(EndpointTest, DetectsMulticastAddresses) {
    EXPECT_FALSE(Endpoint(192, 168, 1, 10, 5000).is_multicast());
    EXPECT_TRUE(Endpoint(239, 255, 10, 1, 5000).is_multicast());
    EXPECT_TRUE(Endpoint(224, 0, 0, 1, 5000).is_multicast());
}

TEST(EndpointTest, SupportsEquality) {
    EXPECT_EQ(Endpoint(1, 2, 3, 4, 80), Endpoint(1, 2, 3, 4, 80));
    EXPECT_NE(Endpoint(1, 2, 3, 4, 80), Endpoint(1, 2, 3, 4, 81));
    EXPECT_NE(Endpoint(1, 2, 3, 4, 80), Endpoint(1, 2, 3, 5, 80));
}

TEST(EndpointTest, SerializesToRawUint64) {
    Endpoint ep(192, 168, 1, 10, 5000);
    const auto raw = ep.to_raw();
    EXPECT_NE(raw, 0U);

    Endpoint decoded;
    ASSERT_TRUE(Endpoint::from_raw(raw, decoded));
    EXPECT_EQ(decoded, ep);
}

}  // namespace isdm::test