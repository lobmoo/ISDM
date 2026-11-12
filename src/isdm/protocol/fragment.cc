#include "isdm/protocol/fragment.h"

namespace isdm::protocol {

bool FragmentAssembler::feed(DataView datagram, const SourceKey& source,
                             std::vector<std::byte>& complete_out) {
    ByteReader reader(datagram);
    std::uint16_t magic = 0;
    std::uint32_t frame_id = 0;
    std::uint16_t sequence = 0;
    std::uint16_t count = 0;
    if (!reader.read(magic) || !reader.read(frame_id) || !reader.read(sequence) ||
        !reader.read(count)) {
        return false;
    }
    if (magic != kFragmentMagic || count == 0 || count > kMaxFragments ||
        sequence >= count) {
        return false;
    }

    const DataView slice =
        datagram.sub_view(reader.offset(), datagram.size() - reader.offset());
    if (slice.size() > kMaxFrameBytes) return false;

    PendingKey key{source, frame_id};
    auto it = pending_.find(key);
    if (it == pending_.end()) {
        if (pending_.size() >= kMaxPending) drop_oldest();
        it = pending_.emplace(key, Entry{}).first;
    }

    Entry& entry = it->second;
    entry.count = count;
    entry.last = std::chrono::steady_clock::now();
    const auto existing = entry.slices.find(sequence);
    if (existing != entry.slices.end()) {
        entry.bytes -= existing->second.size();
    }
    entry.slices[sequence] =
        std::vector<std::byte>(slice.data(), slice.data() + slice.size());
    entry.bytes += slice.size();
    if (entry.bytes > kMaxFrameBytes) {
        pending_.erase(it);
        return false;
    }

    if (entry.slices.size() == count) {
        std::vector<std::byte> full;
        full.reserve(entry.bytes);
        for (std::uint16_t i = 0; i < count; ++i) {
            const auto part = entry.slices.find(i);
            if (part == entry.slices.end()) return false;
            full.insert(full.end(), part->second.begin(), part->second.end());
        }
        pending_.erase(it);
        complete_out = std::move(full);
        return true;
    }
    return false;
}

void FragmentAssembler::expire(std::chrono::steady_clock::time_point now,
                               std::chrono::milliseconds timeout) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->second.last > timeout) {
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void FragmentAssembler::drop_oldest() {
    auto oldest = pending_.begin();
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->second.last < oldest->second.last) oldest = it;
    }
    if (oldest != pending_.end()) pending_.erase(oldest);
}

}  // namespace isdm::protocol
