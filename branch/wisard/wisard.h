#ifndef BRANCH_WISARD_H
#define BRANCH_WISARD_H

#include <array>
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

#include "modules.h"
#include "discriminator.h"
#include "msl/bits.h"
#include "msl/fwcounter.h"

class O3_CPU;

class wisard : public champsim::modules::branch_predictor {
private:
    //hardware knobs
    //this specific config is equal to 64KB
    constexpr static uint32_t NUM_RAM_NODES = 64;
    constexpr static uint8_t TUPLE_BITS = 10;
    constexpr static uint8_t COUNTER_BITS = 8;
    constexpr static uint64_t DECAY_INTERVAL = 1ULL << 18;

    //history knobs
    constexpr static std::size_t PC_LENGTH = 24;
    constexpr static std::size_t GHR_LENGTH = 24;
    constexpr static std::size_t GPHR_LENGTH = 64;
    constexpr static std::size_t PC_BITS_TO_SHIFT = 8;

    //discriminators
    Discriminator disc_taken;
    Discriminator disc_not_taken;

    //local history tables
    //lengths 24, 16, 9, 7, 5
    constexpr static uint32_t NUM_LHTS = 5;
    constexpr static std::array<uint32_t, NUM_LHTS> LHT_SIZES = {1024, 1024, 1024, 1024, 1024};
    constexpr static std::array<uint32_t, NUM_LHTS> LHT_LENGTHS = {24, 16, 9, 7, 5};
    std::vector<std::vector<uint32_t>> lhts;

    //architectural states
    std::bitset<GHR_LENGTH> spec_global_history;
    std::bitset<GHR_LENGTH> real_global_history;
    uint64_t global_path_history = 0;

    std::vector<std::vector<int>> static_ram_mapping;

    //used for passing values from prediction to training
    struct wisard_result {
        std::vector<uint64_t> addresses;
        uint32_t sum_taken = 0;
        uint32_t sum_not_taken = 0;
        bool prediction = false;
    };

    wisard_result last_result;
    uint64_t branches_since_decay = 0;

    std::vector<uint64_t> compute_ram_addresses(champsim::address pc);
    bool apply_bleaching(const std::vector<uint64_t>& addresses);

public:
    wisard(O3_CPU* cpu);
    bool predict_branch(champsim::address pc);
    void last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type);
};

#endif
