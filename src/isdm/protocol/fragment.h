#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include "isdm/codec/codec.h"
#include "isdm/core/data_view.h"

namespace isdm::protocol {

constexpr std::uint16_t kFragmentMagic = 0x4652;  // "FR"
constexpr std::size_t kFragmentHeaderSize = 10;   // magic2 + frame_id4 + seq2 + count2

// Splits a (possibly large) frame into max_slice-sized fragments and emits each
// fragment datagram via emit(). The receiver reassembles them in order.
template <typename F>
void fragment_frame(std::uint32_t frame_id, DataView frame, std::size_t max_slice, F&& emit) {
    const std::size_t total = frame.size();
    const std::size_t count =
        total == 0 ? 1 : (total + max_slice - 1) / max_slice;
    for (std::size_t seq = 0; seq < count; ++seq) {
        const std::size_t offset = seq * max_slice;
        const std::size_t len = std::min(max_slice, total - offset);
        std::vector<std::byte> datagram(kFragmentHeaderSize + len);
        ByteWriter writer(MutableDataView(datagram.data(), datagram.size()));
        writer.write(kFragmentMagic);
        writer.write(frame_id);
        writer.write(static_cast<std::uint16_t>(seq));
        writer.write(static_cast<std::uint16_t>(count));
        if (len != 0) writer.write_bytes(frame.data() + offset, len);
        emit(DataView(datagram.data(), datagram.size()));
    }
}

// Reassembles fragmented datagrams into complete frames. Non-thread-safe; each
// receiving Node must own one assembler on its data thread.
class FragmentAssembler {
public:
    static constexpr std::size_t kMaxPending = 4096;

    // Feeds one datagram. Returns true and fills complete_out when a full frame
    // has been reassembled (the entry is then consumed).
    bool feed(DataView datagram, std::vector<std::byte>& complete_out) {
        ByteReader reader(datagram);
        std::uint16_t magic = 0;
        std::uint32_t frame_id = 0;
        std::uint16_t sequence = 0;
        std::uint16_t count = 0;
        if (!reader.read(magic) || !reader.read(frame_id) || !reader.read(sequence) ||
            !reader.read(count)) {
            return false;
        }
        if (magic != kFragmentMagic || count == 0 || sequence >= count) return false;

        const DataView slice =
            datagram.sub_view(reader.offset(), datagram.size() - reader.offset());
        auto& entry = pending_[frame_id];
        entry.count = count;
        entry.last = std::chrono::steady_clock::now();
        entry.slices[sequence] =
            std::vector<std::byte>(slice.data(), slice.data() + slice.size());

        if (pending_.size() > kMaxPending) {
            drop_oldest();
        }

        if (entry.slices.size() == count) {
            std::vector<std::byte> full;
            for (std::uint16_t i = 0; i < count; ++i) {
                const auto& part = entry.slices[i];
                full.insert(full.end(), part.begin(), part.end());
            }
            pending_.erase(frame_id);
            complete_out = std::move(full);
            return true;
        }
        return false;
    }

    // Drops incomplete fragment groups that have seen no update longer than
    // timeout, bounding memory even when fragments are lost on the wire.
    void expire(std::chrono::steady_clock::time_point now,
                std::chrono::milliseconds timeout) {
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now - it->second.last > timeout) {
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::size_t pending_count() const { return pending_.size(); }

private:
    void drop_oldest() {
        auto oldest = pending_.begin();
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->second.last < oldest->second.last) oldest = it;
        }
        if (oldest != pending_.end()) pending_.erase(oldest);
    }

    struct Entry {
        std::uint16_t count = 0;
        std::chrono::steady_clock::time_point last;
        std::map<std::uint16_t, std::vector<std::byte>> slices;
    };
    std::unordered_map<std::uint32_t, Entry> pending_;
};

}  // namespace isdm::protocol