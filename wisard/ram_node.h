#pragma once

#include <cstdint>
#include <unordered_map>
#include <cstddef>

class RamNode {
private:
    std::unordered_map<uint64_t, uint32_t> storage; // address to counter
    uint32_t max_count; // 2^ctr_bits - 1
public:
    RamNode(uint8_t counter_bits) : max_count(counter_bits >= 32 ? 0xFFFFFFFFu :(1u <<counter_bits) - 1u) {}

    //train
    void train(uint64_t addr) {
        if (storage[addr] < max_count) {
            storage[addr]++;
        }
    }

    //getters
/*
    uint32_t read_counter(uint64_t addr) const {
        auto it = storage.find(addr);
        if(it != storage.end()) {
            return it->second;
        } else {
            return 0;
        }
    }
*/    
    uint8_t read_bleached(uint64_t address, uint32_t threshold) const {
        auto it = storage.find(address);
        if (it == storage.end())
            return 0;
        return (it->second > threshold ? 1 : 0);
    }
};
