#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "isdm/core/buffer.h"

namespace isdm {

class BufferPool {
public:
    struct Config {
        std::size_t small_count = 4096;
        std::size_t medium_count = 1024;
        std::size_t large_count = 256;
        std::size_t huge_count = 32;
    };

    explicit BufferPool(const Config& config);
    BufferPool();

    Buffer acquire(std::size_t minimum_size);

private:
    struct Block {
        std::unique_ptr<std::byte[]> storage;
        std::size_t capacity = 0;
        bool in_use = false;
    };

    struct State {
        std::mutex mutex;
        std::vector<Block> blocks;
    };

    static constexpr std::array<std::size_t, 4> kClassSizes = {
        4U * 1024U, 16U * 1024U, 64U * 1024U, 256U * 1024U};

    std::shared_ptr<State> state_;
};

inline BufferPool::BufferPool() : BufferPool(Config{}) {}

inline BufferPool::BufferPool(const Config& config) : state_(std::make_shared<State>()) {
    const std::array<std::size_t, 4> counts = {
        config.small_count, config.medium_count, config.large_count, config.huge_count};
    std::size_t total = 0;
    for (auto count : counts) total += count;
    state_->blocks.reserve(total);
    for (std::size_t class_index = 0; class_index < kClassSizes.size(); ++class_index) {
        for (std::size_t i = 0; i < counts[class_index]; ++i) {
            Block block;
            block.storage = std::make_unique<std::byte[]>(kClassSizes[class_index]);
            block.capacity = kClassSizes[class_index];
            state_->blocks.push_back(std::move(block));
        }
    }
}

inline Buffer BufferPool::acquire(std::size_t minimum_size) {
    if (minimum_size == 0) return {};
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (std::size_t index = 0; index < state_->blocks.size(); ++index) {
        auto& block = state_->blocks[index];
        if (block.in_use || block.capacity < minimum_size) continue;
        block.in_use = true;
        auto state = state_;
        auto owner = std::shared_ptr<void>(block.storage.get(), [state, index](void*) {
            std::lock_guard<std::mutex> release_lock(state->mutex);
            state->blocks[index].in_use = false;
        });
        return Buffer(block.storage.get(), block.capacity, std::move(owner));
    }
    return {};
}

}  // namespace isdm
