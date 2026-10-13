#include "static_storage.hpp"
#include "h264_padding.h"
#include "uvcl_packet.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

/* CHECK intentionally remains active in NDEBUG builds. Regression checks
 * must not disappear along with the firmware's development assertions. */
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "Check failed at %d: %s\n", __LINE__, #expr); std::abort(); } } while (0)

int main() {
    using camera::FrameState;
    camera::FramePool<2> pool;
    auto first = pool.acquire(FrameState::Capturing, 1);
    auto second = pool.acquire(FrameState::Capturing, 1);
    CHECK(pool.acquire(FrameState::Capturing, 1).index == 2);
    CHECK(!pool.transition(first, FrameState::Ready, FrameState::Free));
    CHECK(pool.transition(first, FrameState::Capturing, FrameState::Ready));
    CHECK(pool.transition(first, FrameState::Ready, FrameState::Encoding));
    CHECK(pool.transition(first, FrameState::Encoding, FrameState::UsbOwned));
    CHECK(pool.transition(first, FrameState::UsbOwned, FrameState::Free));
    CHECK(!pool.transition(first, FrameState::UsbOwned, FrameState::Free));
    auto newer = pool.acquire(FrameState::UsbOwned, 2);
    CHECK(newer.index == first.index);
    CHECK(!pool.transition(first, FrameState::UsbOwned, FrameState::Free));
    CHECK(pool.count(FrameState::UsbOwned) == 1);
    CHECK(pool.transition(newer, FrameState::UsbOwned, FrameState::Free));
    CHECK(pool.transition(second, FrameState::Capturing, FrameState::Free));
    CHECK(pool.count(FrameState::Free) == 2);

    // Reusing a lower index must not overtake an older ready frame.
    first = pool.acquire(FrameState::Ready, 3);
    second = pool.acquire(FrameState::Ready, 3);
    CHECK(pool.transition(first, FrameState::Ready, FrameState::Free));
    newer = pool.acquire(FrameState::Ready, 3);
    CHECK(pool.find(FrameState::Ready).index == second.index);
    CHECK(pool.transition(second, FrameState::Ready, FrameState::Free));
    CHECK(pool.find(FrameState::Ready).index == newer.index);
    CHECK(pool.acquire(FrameState::Free, 3).index == 2);

    alignas(32) std::byte storage[256]{};
    camera::LinearArena arena{storage};
    for (const auto alignment : {8U, 16U, 32U}) {
        arena.reset();
        CHECK(!arena.allocate(1, alignment).empty());
        const auto aligned = arena.allocate(3, alignment);
        CHECK(!aligned.empty());
        CHECK(reinterpret_cast<std::uintptr_t>(aligned.data()) % alignment == 0);
        CHECK(arena.allocate(std::numeric_limits<std::size_t>::max(), alignment).empty());
        CHECK(arena.allocate(1, alignment).size() == 1);
        CHECK(arena.allocate(1, 3).empty());
    }
    CHECK(arena.highWater() <= sizeof(storage));
    arena.reset();
    CHECK(arena.allocate(sizeof(storage), 32).size() == sizeof(storage));
    CHECK(arena.allocate(1, 32).empty());

    camera::EncoderClock clock;
    clock.configure(30);
    CHECK(clock.increment(100) == 0);
    clock.commit(100);
    CHECK(clock.increment(133) == 990);
    CHECK(clock.increment(167) == 2010); // Failed frame at 133 did not commit.
    clock.commit(167);
    CHECK(clock.increment(1200) == 30000); // Long pauses stay bounded.
    clock.commit(UINT32_MAX - 10U);
    CHECK(clock.increment(22) == 990); // Wrap-safe millisecond subtraction.

    std::uint64_t cases = 0;
    /* Sweep every possible frame length through one full output buffer at
     * both bus speeds, including all remainders and multiple exact packets. */
    for (const int packet : {1023, 3072}) {
        for (int size = 1; size <= 65536; ++size) {
            int count, last;
            CHECK(UVCL_PlanPackets(size, packet, &count, &last) == 0);
            CHECK(count > 0 && last > 0 && last <= packet - 2);
            CHECK(static_cast<std::int64_t>(count - 1) * (packet - 2) + last == size);
            ++cases;
        }
        int count, last;
        CHECK(UVCL_PlanPackets(INT_MAX, packet, &count, &last) == 0);
        CHECK(static_cast<std::int64_t>(count - 1) * (packet - 2) + last == INT_MAX);
    }
    int count, last;
    CHECK(UVCL_PlanPackets(0, 3072, &count, &last) != 0);
    CHECK(UVCL_PlanPackets(-1, 3072, &count, &last) != 0);
    CHECK(UVCL_PlanPackets(1, 2, &count, &last) != 0);

    alignas(8) std::uint8_t bytes[40];
    for (std::size_t offset = 0; offset < 8; ++offset) {
        std::memset(bytes, 0x55, sizeof(bytes));
        std::size_t written = 123;
        CHECK(H264_AppendPadding(bytes + offset, 16, &written) == 0);
        CHECK((reinterpret_cast<std::uintptr_t>(bytes + offset) + written) % 8 == 0);
        CHECK(bytes[offset + written] == 0x55);
        if (written) {
            CHECK(bytes[offset + 3] == 1 && bytes[offset + 4] == 0x0c);
            CHECK(bytes[offset + written - 1] == 0x80);
            for (std::size_t i = 5; i + 1 < written; ++i) CHECK(bytes[offset + i] == 0xff);
            std::memset(bytes, 0x55, sizeof(bytes));
            CHECK(H264_AppendPadding(bytes + offset, written - 1, &written) != 0);
            for (const auto byte : bytes) CHECK(byte == 0x55);
        }
    }
    std::printf("Passed pool generation/release, arena overflow/alignment, padding and %llu packet cases\n",
                static_cast<unsigned long long>(cases));
}
