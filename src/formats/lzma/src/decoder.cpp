// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The piece-by-piece decoder over the vendored LZMA SDK decode.

#include "oa/formats/lzma.hpp"

#include "Lzma2Dec.h"
#include "LzmaDec.h"

#include <cstdlib>

namespace oa::formats::lzma {
namespace {

/// Bytes of the LZMA properties header before the compressed bytes.
constexpr uint8_t lzma_header_bytes = 13;

/// Bytes of the LZMA2 dictionary property before the compressed bytes.
constexpr uint8_t lzma2_header_bytes = 1;

/// Allocation the SDK calls. A request of no bytes still returns a pointer.
extern "C" void* allocate_bytes(ISzAllocPtr, size_t size) {
    return std::malloc(size == 0 ? 1 : size);
}

/// Frees a pointer from allocate_bytes. A null pointer is left alone.
extern "C" void free_bytes(ISzAllocPtr, void* address) {
    std::free(address);
}

const ISzAlloc allocator{allocate_bytes, free_bytes};

/// Reads a little-endian unsigned value of `count` bytes.
///
/// @param bytes the first byte
/// @param count how many bytes, from 1 to 8
/// @return the value
uint64_t read_le(const uint8_t* bytes, int count) {
    uint64_t value = 0;
    for (int index = 0; index < count; ++index)
        value |= static_cast<uint64_t>(bytes[index]) << (8 * index);
    return value;
}

/// The dictionary size an LZMA2 property byte names, or 0 when the byte is not a property.
///
/// @param prop the property byte
/// @return the dictionary size in bytes
uint32_t lzma2_dictionary(uint8_t prop) {
    if (prop > 40)
        return 0;
    if (prop == 40)
        return 0xffffffffu;
    const uint32_t base = 2u | (prop & 1u);
    return base << (static_cast<unsigned>(prop) / 2u + 11u);
}

/// One decode call, writing into `output` and taking from `input`.
struct Step {
    SRes code{SZ_ERROR_DATA};
    SizeT consumed{0};
    SizeT produced{0};
    ELzmaStatus status{LZMA_STATUS_NOT_SPECIFIED};
};

/// Decodes one LZMA step.
///
/// @param state the SDK decoder
/// @param input compressed bytes not yet offered
/// @param output room for decoded bytes
/// @param mode whether this room ends the stream
/// @return what the SDK reported
Step step_lzma(
    CLzmaDec* state, std::span<const uint8_t> input, std::span<uint8_t> output, ELzmaFinishMode mode
) {
    Step step;
    step.consumed = input.size();
    step.produced = output.size();
    step.code = LzmaDec_DecodeToBuf(
        state, output.data(), &step.produced, input.data(), &step.consumed, mode, &step.status
    );
    return step;
}

/// Decodes one LZMA2 step.
///
/// @param state the SDK decoder
/// @param input compressed bytes not yet offered
/// @param output room for decoded bytes
/// @param mode whether this room ends the stream
/// @return what the SDK reported
Step step_lzma2(
    CLzma2Dec* state,
    std::span<const uint8_t> input,
    std::span<uint8_t> output,
    ELzmaFinishMode mode
) {
    Step step;
    step.consumed = input.size();
    step.produced = output.size();
    step.code = Lzma2Dec_DecodeToBuf(
        state, output.data(), &step.produced, input.data(), &step.consumed, mode, &step.status
    );
    return step;
}

} // namespace

Decoder::Decoder(Kind kind) : kind_(kind) {
}

Decoder::~Decoder() {
    if (state_ == nullptr)
        return;
    if (kind_ == Kind::lzma)
        LzmaDec_Free(static_cast<CLzmaDec*>(state_), &allocator);
    else
        Lzma2Dec_Free(static_cast<CLzma2Dec*>(state_), &allocator);
    std::free(state_);
}

bool Decoder::open() {
    if (kind_ == Kind::lzma) {
        const uint32_t dictionary = static_cast<uint32_t>(read_le(header_ + 1, 4));
        if (dictionary == 0 || dictionary > max_dictionary_bytes)
            return false;
        uncompressed_size_ = read_le(header_ + 5, 8);
        size_known_ = uncompressed_size_ != UINT64_MAX;
        auto* state = static_cast<CLzmaDec*>(std::malloc(sizeof(CLzmaDec)));
        if (state == nullptr)
            return false;
        LzmaDec_Construct(state);
        if (LzmaDec_Allocate(state, header_, LZMA_PROPS_SIZE, &allocator) != SZ_OK) {
            std::free(state);
            return false;
        }
        LzmaDec_Init(state);
        state_ = state;
        return true;
    }

    const uint32_t dictionary = lzma2_dictionary(header_[0]);
    if (dictionary == 0 || dictionary > max_dictionary_bytes)
        return false;
    auto* state = static_cast<CLzma2Dec*>(std::malloc(sizeof(CLzma2Dec)));
    if (state == nullptr)
        return false;
    Lzma2Dec_Construct(state);
    if (Lzma2Dec_Allocate(state, header_[0], &allocator) != SZ_OK) {
        std::free(state);
        return false;
    }
    Lzma2Dec_Init(state);
    state_ = state;
    return true;
}

Feed Decoder::feed(std::span<const uint8_t> input, std::span<uint8_t> output) {
    Feed result;
    if (failed_) {
        result.status = FeedStatus::error;
        return result;
    }
    if (finished_) {
        result.status = FeedStatus::finished;
        return result;
    }

    size_t input_at = 0;
    if (!opened_) {
        const uint8_t need = kind_ == Kind::lzma ? lzma_header_bytes : lzma2_header_bytes;
        while (header_filled_ < need && input_at < input.size())
            header_[header_filled_++] = input[input_at++];
        result.consumed = input_at;
        if (header_filled_ < need) {
            result.status = FeedStatus::need_input;
            return result;
        }
        if (!open()) {
            failed_ = true;
            result.status = FeedStatus::error;
            return result;
        }
        opened_ = true;
    }

    while (!finished_ && !failed_) {
        if (result.produced == output.size()) {
            result.status = result.produced == 0 && result.consumed == 0 ? FeedStatus::need_output
                                                                         : FeedStatus::ready;
            return result;
        }
        if (input_at == input.size()) {
            result.status = result.produced == 0 ? FeedStatus::need_input : FeedStatus::ready;
            return result;
        }

        const uint64_t remaining = size_known_ ? uncompressed_size_ - produced_total_ : UINT64_MAX;
        // Once every declared byte is written, one more byte of room lets the
        // decoder read an end mark that arrived in a later piece. A byte
        // written there means the stream is longer than its header said.
        uint8_t past_end = 0;
        SizeT room = output.size() - result.produced;
        ELzmaFinishMode mode = LZMA_FINISH_ANY;
        std::span<uint8_t> next_out = output.subspan(result.produced, room);
        if (remaining <= room) {
            room = static_cast<SizeT>(remaining);
            mode = LZMA_FINISH_END;
            next_out = output.subspan(result.produced, room);
        }
        if (remaining == 0) {
            room = 1;
            mode = LZMA_FINISH_END;
            next_out = std::span<uint8_t>(&past_end, 1);
        }
        const std::span<const uint8_t> next_in = input.subspan(input_at);
        const Step step =
            kind_ == Kind::lzma
                ? step_lzma(static_cast<CLzmaDec*>(state_), next_in, next_out, mode)
                : step_lzma2(static_cast<CLzma2Dec*>(state_), next_in, next_out, mode);
        if (step.consumed == 0 && step.produced == 0) {
            failed_ = true;
            result.status = FeedStatus::error;
            return result;
        }
        input_at += step.consumed;
        result.consumed = input_at;
        if (remaining == 0) {
            if (step.produced != 0) {
                failed_ = true;
                result.status = FeedStatus::error;
                return result;
            }
        } else {
            result.produced += step.produced;
            produced_total_ += step.produced;
        }
        if (step.code != SZ_OK) {
            failed_ = true;
            result.status = FeedStatus::error;
            return result;
        }
        const bool marked = step.status == LZMA_STATUS_FINISHED_WITH_MARK;
        const bool complete = size_known_ && produced_total_ == uncompressed_size_ &&
                              (marked || step.status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK);
        if (marked || complete) {
            finished_ = true;
            break;
        }
        if (step.status == LZMA_STATUS_NEEDS_MORE_INPUT && input_at == input.size()) {
            result.status = FeedStatus::need_input;
            return result;
        }
    }

    result.status = failed_ ? FeedStatus::error : FeedStatus::finished;
    return result;
}

FeedStatus Decoder::finish() {
    if (failed_)
        return FeedStatus::error;
    if (!finished_)
        failed_ = true;
    return failed_ ? FeedStatus::error : FeedStatus::finished;
}

} // namespace oa::formats::lzma
