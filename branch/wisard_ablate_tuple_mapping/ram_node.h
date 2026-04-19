#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

class RamNode {
private:
    std::vector<uint32_t> counters;
    uint32_t max_count;
    uint64_t address_mask;

public:
    RamNode(uint8_t tuple_bits, uint8_t counter_bits)
        : counters(std::size_t{1} << tuple_bits, 0),
          max_count(counter_bits >= 32 ? std::numeric_limits<uint32_t>::max()
                                       : ((uint32_t{1} << counter_bits) - 1)),
          address_mask(tuple_bits >= 64 ? std::numeric_limits<uint64_t>::max()
                                        : ((uint64_t{1} << tuple_bits) - 1))
    {
        assert(tuple_bits < std::numeric_limits<std::size_t>::digits);
    }

    void train(uint64_t address)
    {
        uint32_t& ctr = counters[address & address_mask];
        if (ctr < max_count) {
            ++ctr;
        }
    }

    uint8_t read_bleached(uint64_t address, uint32_t threshold) const
    {
        return counters[address & address_mask] > threshold ? 1u : 0u;
    }

    void decay()
    {
        for (uint32_t& ctr : counters) {
            ctr >>= 1;
        }
    }

    uint32_t read_counter(uint64_t address) const
    {
        return counters[address & address_mask];
    }

    std::size_t size() const
    {
        return counters.size();
    }
};
