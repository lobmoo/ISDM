// ISDM performance benchmark (informational; no assertions).
// Measures: struct codec, batch codec, SPSC queue, and end-to-end
// publish -> subscribe throughput + latency.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "isdm/isdm.h"
#include "isdm/reflect.h"
#include "isdm/runtime/spsc_queue.h"

namespace bench {

struct Msg {
    std::uint32_t id;
    float a;
    float b;
    float c;
    double d;
};

// A larger struct so field-level Delta shows its bandwidth win.
struct Plant {
    std::uint32_t id;
    float p0, p1, p2, p3, p4, p5, p6, p7;
};

}  // namespace bench

ISDM_STRUCT(bench::Msg, ISDM_FIELD(bench::Msg, id), ISDM_FIELD(bench::Msg, a),
            ISDM_FIELD(bench::Msg, b), ISDM_FIELD(bench::Msg, c), ISDM_FIELD(bench::Msg, d))

ISDM_STRUCT(bench::Plant, ISDM_FIELD(bench::Plant, id), ISDM_FIELD(bench::Plant, p0),
            ISDM_FIELD(bench::Plant, p1), ISDM_FIELD(bench::Plant, p2),
            ISDM_FIELD(bench::Plant, p3), ISDM_FIELD(bench::Plant, p4),
            ISDM_FIELD(bench::Plant, p5), ISDM_FIELD(bench::Plant, p6),
            ISDM_FIELD(bench::Plant, p7))

namespace {

using Clock = std::chrono::steady_clock;

isdm::NodeConfig make_config(isdm::NodeId id, const char* name, std::uint16_t port) {
    isdm::NodeConfig cfg;
    cfg.node_id = id;
    cfg.node_name = name;
    cfg.capabilities = 0x1;
    cfg.advertise_endpoint = isdm::transport::Endpoint(239, 255, 40, 1, port);
    cfg.control_bind = isdm::transport::Endpoint(0, 0, 0, 0, port);
    cfg.data_bind = isdm::transport::Endpoint(127, 0, 0, 1, 0);
    return cfg;
}

void bench_codec() {
    const auto ref = isdm::describe<bench::Msg>(100, 1);
    bench::Msg in{1, 1.0f, 2.0f, 3.0f, 4.0};
    const std::size_t kIters = 1000000;

    std::array<std::byte, 128> storage{};
    const auto e0 = Clock::now();
    for (std::size_t i = 0; i < kIters; ++i) {
        isdm::ByteWriter w2(isdm::MutableDataView(storage.data(), storage.size()));
        isdm::StructCodec::encode(ref.descriptor(), &in, w2);
    }
    const auto e1 = Clock::now();
    std::printf("codec  encode   %8.1f M ops/s\n",
                kIters / std::chrono::duration<double>(e1 - e0).count() / 1e6);
}

void bench_queue() {
    isdm::SpscQueue<std::uint64_t> q(4096);
    const std::size_t kIters = 5000000;
    const auto t0 = Clock::now();
    for (std::size_t i = 0; i < kIters; ++i) {
        q.try_enqueue(i);
    }
    // drain
    std::uint64_t sink = 0;
    while (q.try_dequeue(sink)) {
    }
    const auto t1 = Clock::now();
    std::printf("queue  spsc     %8.1f M ops/s (enqueue+dequeue)\n",
                2.0 * kIters / std::chrono::duration<double>(t1 - t0).count() / 1e6);
}

void bench_end_to_end() {
    const std::uint16_t port = 42001;
    constexpr std::uint32_t kTypeId = 100;
    constexpr int kObjects = 20000;
    constexpr int kBatch = 1000;

    isdm::Node pub(make_config(1, "publisher", port));
    isdm::Node sub(make_config(2, "subscriber", port));
    pub.register_type_typed<bench::Msg>(kTypeId);
    pub.start();
    sub.start();

    std::atomic<long> received{0};
    sub.subscribe(kTypeId, [&](isdm::ObjectId, isdm::TypeId, isdm::DataView p, isdm::Timestamp) {
        if (p.size() == sizeof(bench::Msg)) ++received;
    });

    for (int i = 0; i < 400 && (pub.peers().size() == 0 || sub.peers().size() == 0); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (pub.peers().size() == 0 || sub.peers().size() == 0) {
        std::printf("e2e    nodes    discovery failed\n");
        return;
    }

    std::vector<isdm::ObjectId> ids(kBatch);
    std::vector<bench::Msg> msgs(kBatch);

    // Warm latency: one object, measure round-trip receive.
    bench::Msg probe{0, 1, 2, 3, 4};
    const auto l0 = Clock::now();
    pub.publish_typed(1, kTypeId, probe);
    while (received.load() == 0) std::this_thread::yield();
    const auto l1 = Clock::now();
    std::printf("e2e    latency   %.2f us\n",
                std::chrono::duration<double, std::micro>(l1 - l0).count());

    received = 0;
    const auto t0 = Clock::now();
    int sent = 0;
    while (sent < kObjects) {
        const int n = std::min(kBatch, kObjects - sent);
        for (int i = 0; i < n; ++i) {
            ids[i] = sent + i + 1;
            msgs[i] = {static_cast<std::uint32_t>(sent + i + 1), 1, 2, 3, 4};
        }
        pub.publish_batch(ids.data(), msgs.data(), n, kTypeId, sizeof(bench::Msg));
        sent += n;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    while (received.load() < kObjects) std::this_thread::yield();
    const auto t1 = Clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    std::printf("e2e    objects  %d delivered in %.2fs (~%.0f objects/s)\n", kObjects, secs,
                kObjects / secs);
    pub.stop();
    sub.stop();
}

void bench_delta_bytes() {
    const auto ref = isdm::describe<bench::Plant>(200, 1);
    bench::Plant a{1, 1, 2, 3, 4, 5, 6, 7, 8};
    bench::Plant b = a;
    b.p5 = 99.0f;  // only one field changes

    std::array<std::byte, 256> storage{};
    isdm::ByteWriter w(isdm::MutableDataView(storage.data(), storage.size()));
    // Delta bytes when one field changes.
    isdm::ByteWriter dw(isdm::MutableDataView(storage.data(), storage.size()));
    const bool encoded = isdm::protocol::encode_delta(ref.descriptor(), 1, 200, 0, &a, &b, dw);

    // Full object bytes.
    const std::size_t full = sizeof(bench::Plant);
    std::size_t delta = 0;
    if (encoded) delta = dw.offset();
    std::printf("delta  full=%zuB changed-1-field=%zuB (%.0f%%)\n", full, delta,
                delta == 0 ? 0.0 : 100.0 * (1.0 - static_cast<double>(delta) / full));
    (void)w;
}

}  // namespace

int main() {
    std::printf("== ISDM benchmark ==\n");
    bench_codec();
    bench_queue();
    bench_delta_bytes();
    bench_end_to_end();
    return 0;
}