#ifndef STATIC_STORAGE_HPP
#define STATIC_STORAGE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace camera {

/* These types deliberately contain no locks or peripheral calls. The owning
 * task supplies synchronization, and the same code can be tested on a host.
 * A handle includes a generation so a completion from a previous stream
 * cannot return a buffer acquired by a newer stream. */
enum class FrameState : std::uint8_t { Free, Capturing, Ready, Encoding, UsbOwned };
struct FrameHandle {
    std::size_t index;
    std::uint32_t generation;
};

template<std::size_t N> class FramePool final {
public:
    static constexpr std::size_t invalid = N;

    FrameHandle acquire(FrameState state, std::uint32_t generation) noexcept {
        if (state == FrameState::Free) return {invalid, generation};
        for (std::size_t i = 0; i < N; ++i) {
            if (states_[i] == FrameState::Free) {
                states_[i] = state;
                generations_[i] = generation;
                tickets_[i] = next_ticket_++;
                return {i, generation};
            }
        }
        return {invalid, generation};
    }

    /* All transitions require the caller's expected previous state. A miss,
     * stale handle or duplicate release leaves the pool completely unchanged. */
    bool transition(FrameHandle handle, FrameState from, FrameState to) noexcept {
        if (handle.index >= N || generations_[handle.index] != handle.generation ||
            states_[handle.index] != from)
            return false;
        states_[handle.index] = to;
        return true;
    }

    FrameHandle find(FrameState state) const noexcept {
        std::size_t oldest = invalid;
        for (std::size_t i = 0; i < N; ++i) {
            if (states_[i] != state) continue;
            /* Ready frames must be consumed in capture order, not array-index
             * order. Wrap-safe tickets prevent one fast reusable slot from
             * starving an older frame and reversing encoder timestamps. */
            if (oldest == invalid || tickets_[i] - tickets_[oldest] > UINT32_MAX / 2U)
                oldest = i;
        }
        return oldest == invalid ? FrameHandle{invalid, 0} : FrameHandle{oldest, generations_[oldest]};
    }

    std::size_t count(FrameState state) const noexcept {
        std::size_t result = 0;
        for (const auto current : states_)
            result += current == state;
        return result;
    }

    FrameHandle handle(std::size_t index) const noexcept {
        return index < N ? FrameHandle{index, generations_[index]} : FrameHandle{invalid, 0};
    }

private:
    std::array<FrameState, N> states_{};
    std::array<std::uint32_t, N> generations_{};
    std::array<std::uint32_t, N> tickets_{};
    std::uint32_t next_ticket_{};
};

/* Encoder timestamps use fps*1000 units per second with a denominator of
 * 1000. This represents captured millisecond intervals without rounding a
 * 33 ms frame down to zero or pretending a dropped frame took no time.
 * Failed encodes do not commit the timestamp. Unsigned subtraction handles
 * tick wrap; long pauses are capped at one second for bounded rate control. */
class EncoderClock final {
public:
    void configure(std::uint32_t fps) noexcept { fps_ = fps; valid_ = false; }
    std::uint32_t increment(std::uint32_t timestamp) const noexcept {
        if (!valid_) return 0;
        const auto elapsed = timestamp - previous_;
        return (elapsed > 1000U ? 1000U : elapsed) * fps_;
    }
    void commit(std::uint32_t timestamp) noexcept { previous_ = timestamp; valid_ = true; }
private:
    std::uint32_t fps_{}, previous_{};
    bool valid_{};
};

/* An offset-based arena avoids forming out-of-object pointers during bounds
 * checks. Storage must be aligned by the owner. Allocation is bounded and
 * failure does not consume space; reset is allowed only after DMA quiescence. */
class LinearArena final {
public:
    explicit constexpr LinearArena(std::span<std::byte> storage) noexcept : storage_(storage) {}

    std::span<std::byte> allocate(std::size_t size, std::size_t alignment) noexcept {
        if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
            return {};
        const auto padding = (alignment - offset_ % alignment) % alignment;
        if (padding > storage_.size() - offset_)
            return {};
        const auto start = offset_ + padding;
        if (size > storage_.size() - start)
            return {};
        offset_ = start + size;
        if (offset_ > high_water_)
            high_water_ = offset_;
        return storage_.subspan(start, size);
    }

    void reset() noexcept { offset_ = 0; }
    std::size_t highWater() const noexcept { return high_water_; }

private:
    std::span<std::byte> storage_;
    std::size_t offset_{};
    std::size_t high_water_{};
};

} // namespace camera
#endif
