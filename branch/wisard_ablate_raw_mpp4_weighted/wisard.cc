#include "wisard.h"

#include <cassert>

#include "instruction.h"

wisard_ablate_raw_mpp4_weighted::wisard_ablate_raw_mpp4_weighted(O3_CPU* cpu)
    : champsim::modules::branch_predictor(cpu),
      disc_taken(NUM_RAM_NODES, TUPLE_BITS, COUNTER_BITS),
      disc_not_taken(NUM_RAM_NODES, TUPLE_BITS, COUNTER_BITS)
{
    (void)cpu;

    for (uint32_t size : LHT_SIZES) {
        lhts.push_back(std::vector<uint32_t>(size, 0));
    }
    last_result.addresses.resize(NUM_RAM_NODES, 0);
    static_ram_mapping.resize(NUM_RAM_NODES, std::vector<int>(TUPLE_BITS));

    assert(std::accumulate(FEATURE_FAMILY_LENGTHS.begin(), FEATURE_FAMILY_LENGTHS.end(), uint32_t{0}) == NUM_INPUT_BITS);

    std::mt19937 rng(67);
    std::discrete_distribution<int> family_dist(FEATURE_FAMILY_WEIGHTS.begin(), FEATURE_FAMILY_WEIGHTS.end());

    std::array<std::vector<int>, NUM_FEATURE_FAMILIES> family_indices;
    std::array<int, NUM_FEATURE_FAMILIES> family_ptrs = {};
    int family_start = 0;

    for (std::size_t family = 0; family < NUM_FEATURE_FAMILIES; ++family) {
        const int family_length = static_cast<int>(FEATURE_FAMILY_LENGTHS[family]);
        family_indices[family].resize(family_length);
        std::iota(family_indices[family].begin(), family_indices[family].end(), family_start);
        std::shuffle(family_indices[family].begin(), family_indices[family].end(), rng);
        family_start += family_length;
    }

    for (uint32_t i = 0; i < NUM_RAM_NODES; ++i) {
        for (uint32_t b = 0; b < TUPLE_BITS; ++b) {
            int family = family_dist(rng);
            if (family_ptrs[family] >= static_cast<int>(family_indices[family].size())) {
                std::shuffle(family_indices[family].begin(), family_indices[family].end(), rng);
                family_ptrs[family] = 0;
            }
            static_ram_mapping[i][b] = family_indices[family][family_ptrs[family]++];
        }
    }
}

uint32_t wisard_ablate_raw_mpp4_weighted::hash_recencypos(uint32_t pc_shifted) const
{
    for (uint32_t i = 0; i < RECENCYPOS_SEARCH_DEPTH; ++i) {
        if (spec_recency_stack[i] == pc_shifted) {
            return (i * RECENCYPOS_BUCKETS) / RECENCYPOS_SEARCH_DEPTH;
        }
    }

    return RECENCYPOS_BUCKETS - 1;
}

void wisard_ablate_raw_mpp4_weighted::insert_recency(std::array<uint32_t, RECENCYPOS_SEARCH_DEPTH>& recency_stack, uint32_t pc_shifted)
{
    std::size_t hit_index = 0;
    for (; hit_index < recency_stack.size(); ++hit_index) {
        if (recency_stack[hit_index] == pc_shifted) {
            break;
        }
    }

    if (hit_index == recency_stack.size()) {
        hit_index = recency_stack.size() - 1;
        recency_stack[hit_index] = pc_shifted;
    }

    const uint32_t entry = recency_stack[hit_index];
    for (std::size_t i = hit_index; i > 0; --i) {
        recency_stack[i] = recency_stack[i - 1];
    }
    recency_stack[0] = entry;
}

std::vector<uint64_t> wisard_ablate_raw_mpp4_weighted::compute_ram_addresses(champsim::address pc)
{
    constexpr uint32_t PC_MASK = (uint32_t{1} << PC_LENGTH) - 1;

    uint64_t pc_24 = pc.slice_lower<champsim::data::bits{PC_LENGTH}>().to<uint64_t>();
    uint64_t ghr_24 = spec_global_history.to_ullong();
    uint64_t pc_xor_ghr = pc_24 ^ ghr_24;
    uint32_t pc_shifted = static_cast<uint32_t>((pc.to<uint64_t>() >> 2) & PC_MASK);
    uint64_t lhr0 = lhts[0][pc_24 % LHT_SIZES[0]] & 0xFFFFFF;
    uint64_t lhr1 = lhts[1][pc_24 % LHT_SIZES[1]] & 0xFFFF;
    uint64_t lhr2 = lhts[2][pc_24 % LHT_SIZES[2]] & 0x1FF;
    uint64_t lhr3 = lhts[3][pc_24 % LHT_SIZES[3]] & 0x7F;
    uint64_t lhr4 = lhts[4][pc_24 % LHT_SIZES[4]] & 0x1F;

    std::vector<uint64_t> addresses(NUM_RAM_NODES);
    std::bitset<NUM_INPUT_BITS> input_vector;
    int offset = 0;

    auto append_bits_once = [&](uint64_t val, int bit_width) {
        for (int b = 0; b < bit_width; ++b) {
            input_vector[offset++] = (val >> b) & 1;
        }
    };

    append_bits_once(pc_24, 24);
    append_bits_once(ghr_24, 24);
    append_bits_once(pc_xor_ghr, 24);
    append_bits_once(lhr0, 24);
    append_bits_once(lhr1, 16);
    append_bits_once(lhr2, 9);
    append_bits_once(lhr3, 7);
    append_bits_once(lhr4, 5);
    append_bits_once(global_path_history, 64);

    for (uint32_t i = 0; i < MODHIST_MOD5_DEPTH; ++i) {
        input_vector[offset++] = spec_modhist_mod5[i] & 1u;
    }
    for (uint32_t i = 0; i < MODHIST_MOD7_DEPTH; ++i) {
        input_vector[offset++] = spec_modhist_mod7[i] & 1u;
    }
    append_bits_once((ghr_24 << 3) ^ pc_24, 24);
    append_bits_once((ghr_24 << 5) ^ pc_24, 24);
    append_bits_once((ghr_24 << 7) ^ pc_24, 24);
    append_bits_once(ghr_24 >> 2, 22);
    append_bits_once(ghr_24 >> 3, 21);
    append_bits_once(hash_recencypos(pc_shifted), RECENCYPOS_BITS);
    append_bits_once(imli_counter != 0 ? 1u : 0u, 1);
    append_bits_once(imli_counter, IMLI_LITE_COUNTER_BITS);

    assert(offset == static_cast<int>(NUM_INPUT_BITS));

    for (uint32_t i = 0; i < NUM_RAM_NODES; ++i) {
        uint64_t addr = 0;
        for (uint32_t b = 0; b < TUPLE_BITS; ++b) {
            int mapped_index = static_ram_mapping[i][b];
            if (input_vector[mapped_index]) {
                addr |= (1ULL << b);
            }
        }
        addresses[i] = addr;
    }

    return addresses;
}

bool wisard_ablate_raw_mpp4_weighted::apply_bleaching(const std::vector<uint64_t>& addresses)
{
    uint32_t b = 1;
    uint32_t max_val = (1 << COUNTER_BITS) - 1;

    while (b <= max_val) {
        uint32_t sum_not_taken = disc_not_taken.get_response(addresses, b);
        uint32_t sum_taken = disc_taken.get_response(addresses, b);

        if (sum_taken > sum_not_taken) return true;
        if (sum_not_taken > sum_taken) return false;
        b++;
    }
    return false;
}

bool wisard_ablate_raw_mpp4_weighted::predict_branch(champsim::address pc)
{
    constexpr uint32_t PC_MASK = (uint32_t{1} << PC_LENGTH) - 1;

    last_result.addresses = compute_ram_addresses(pc);
    last_result.sum_not_taken = disc_not_taken.get_response(last_result.addresses, 0);
    last_result.sum_taken = disc_taken.get_response(last_result.addresses, 0);

    if (last_result.sum_taken > last_result.sum_not_taken) {
        last_result.prediction = true;
    } else if (last_result.sum_not_taken > last_result.sum_taken) {
        last_result.prediction = false;
    } else {
        last_result.prediction = apply_bleaching(last_result.addresses);
    }

    const uint32_t pc_shifted = static_cast<uint32_t>((pc.to<uint64_t>() >> 2) & PC_MASK);
    const uint8_t predicted_bit = last_result.prediction ? 1u : 0u;

    insert_recency(spec_recency_stack, pc_shifted);
    if ((pc_shifted % 5u) == 0) {
        push_history(spec_modhist_mod5, predicted_bit);
    }
    if ((pc_shifted % 7u) == 0) {
        push_history(spec_modhist_mod7, predicted_bit);
    }

    spec_global_history <<= 1;
    spec_global_history.set(0, last_result.prediction);

    return last_result.prediction;
}

void wisard_ablate_raw_mpp4_weighted::last_branch_result(champsim::address pc_addr, champsim::address branch_target, bool taken, uint8_t branch_type)
{
    constexpr uint32_t PC_MASK = (uint32_t{1} << PC_LENGTH) - 1;

    uint64_t pc = pc_addr.to<uint64_t>();
    const uint32_t pc_shifted = static_cast<uint32_t>((pc >> 2) & PC_MASK);
    const uint8_t taken_bit = taken ? 1u : 0u;
    const bool is_conditional = branch_type == BRANCH_CONDITIONAL;
    const bool is_backward = branch_target.to<uint64_t>() < pc;

    if (++branches_since_decay >= DECAY_INTERVAL) {
        disc_taken.decay();
        disc_not_taken.decay();
        branches_since_decay = 0;
    }

    if (taken) {
        disc_taken.train(last_result.addresses);
    } else {
        disc_not_taken.train(last_result.addresses);
    }

    real_global_history <<= 1;
    real_global_history.set(0, taken);

    insert_recency(real_recency_stack, pc_shifted);
    if ((pc_shifted % 5u) == 0) {
        push_history(real_modhist_mod5, taken_bit);
    }
    if ((pc_shifted % 7u) == 0) {
        push_history(real_modhist_mod7, taken_bit);
    }

    if (last_result.prediction != taken) {
        spec_global_history = real_global_history;
        spec_recency_stack = real_recency_stack;
        spec_modhist_mod5 = real_modhist_mod5;
        spec_modhist_mod7 = real_modhist_mod7;
    }

    if (is_conditional && is_backward) {
        if (taken) {
            if (imli_counter < ((1u << IMLI_LITE_COUNTER_BITS) - 1)) {
                ++imli_counter;
            }
        } else {
            imli_counter = 0;
        }
    } else if (!taken) {
        imli_counter = 0;
    }

    for (uint32_t i = 0; i < NUM_LHTS; i++) {
        uint64_t lht_idx = pc % LHT_SIZES[i];
        uint32_t width = LHT_LENGTHS[i];
        uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((uint32_t{1} << width) - 1);
        lhts[i][lht_idx] = ((lhts[i][lht_idx] << 1) | (taken ? 1u : 0u)) & mask;
    }

    global_path_history <<= PC_BITS_TO_SHIFT;
    uint64_t pc_bits = pc_addr.slice_lower<champsim::data::bits{PC_BITS_TO_SHIFT}>().to<uint64_t>();
    global_path_history ^= pc_bits;
    global_path_history &= ((1ULL << GPHR_LENGTH) - 1);
}
