/**
 * Static camera/encoder/USB orchestration for NUCLEO-N657X0-Q.
 * Derived from the ST camera capture application.
 * Copyright (c) 2024 STMicroelectronics. All rights reserved.
 * Distributed under the license in Inc/LICENSE.md.
 */
#include "app.h"
#include "app_cam.h"
#include "app_enc.h"
#include "app_config.h"
#include "app_fault.h"
#include "board_memory.h"
#include "static_storage.hpp"
extern "C" {
#include "cmw_camera.h"
#include "stm32n6xx_nucleo.h"
#include "stm32n6xx_ll_venc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "uvcl.h"
}
#include <cstdio>
#include <cstdint>

namespace {
/* Storage is internal AXI SRAM, not external PSRAM. Its NOLOAD placement
 * lets it reuse FSBL RAM after boot, and MPU attributes provide DMA coherence.
 * Each row is cache-line aligned because both capacities are multiples of 32. */
constexpr std::size_t bufferCount = 2;
constexpr int width = 480, height = 272, fps = 30;
alignas(32) std::uint8_t captureStorage[bufferCount][MAX_IMG_FRAME_SIZE]
    __attribute__((section(".dma_bss")));
alignas(32) std::uint8_t encodedStorage[bufferCount][H264_OUT_BUFFER_SIZE]
    __attribute__((section(".dma_bss")));
static_assert(MAX_IMG_FRAME_SIZE >= width * height * 2);
static_assert(MAX_IMG_FRAME_SIZE % 32 == 0 && H264_OUT_BUFFER_SIZE % 32 == 0);
static_assert(sizeof(captureStorage) + sizeof(encodedStorage) + BOARD_VENC_ARENA_SIZE <= BOARD_DMA_RAM_SIZE,
              "Capture, output and encoder arena exceed the Nucleo SRAM budget");
static_assert(width % 16 == 0 && height % 16 == 0);

/* Pool/request metadata is shared by task callbacks and USB reset callbacks.
 * Interrupt masking protects only short, bounded metadata operations; never
 * hold this guard across HAL, encoding, logging or a blocking FreeRTOS call. */
class InterruptGuard final {
public:
    InterruptGuard() noexcept : flags_(__get_PRIMASK()) { __disable_irq(); __DMB(); }
    ~InterruptGuard() { __DMB(); __set_PRIMASK(flags_); }
    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;
private:
    std::uint32_t flags_;
};

/* A lifecycle mutex serializes camera init/deinit with the sole camera worker.
 * Taking it during stop is the quiescence handshake: when obtained, no worker
 * can still access the sensor or DCMIPP while their resources are released. */
class CameraGuard final {
public:
    explicit CameraGuard(SemaphoreHandle_t lock) : lock_(lock) {
        APP_REQUIRE(xSemaphoreTake(lock_, pdMS_TO_TICKS(2000)) == pdTRUE);
    }
    ~CameraGuard() { APP_REQUIRE(xSemaphoreGive(lock_) == pdTRUE); }
private:
    SemaphoreHandle_t lock_;
};

class StreamController final {
public:
    StreamController() = delete;
    static void initialize() {
        cameraMutex_ = xSemaphoreCreateMutexStatic(&cameraMutexStorage_);
        APP_REQUIRE(cameraMutex_);
        /* Create notification destinations before enabling any peripheral IRQ.
         * The controller starts blocked until an explicit host stream request. */
        controllerTask_ = xTaskCreateStatic(run, "stream", 2048, nullptr, 8,
                                            controllerStack_, &controllerTcb_);
        workerTask_ = xTaskCreateStatic(cameraWorker, "camera", 2048, nullptr, 11,
                                       workerStack_, &workerTcb_);
        APP_REQUIRE(controllerTask_ && workerTask_);
        __HAL_RCC_SYSCFG_CLK_ENABLE();
        LL_VENC_Init();
        UVCL_Conf_t config{};
        const UVCL_StreamConf_t streams[] = IMG_STREAMS;
        static_assert(sizeof(streams) / sizeof(streams[0]) <= UVCL_MAX_STREAM_CONF_NB);
        for (const auto& stream : streams) config.streams[config.streams_nb++] = stream;
        config.is_immediate_mode = 1;
        callbacks_.streaming_active = streamingActive;
        callbacks_.streaming_inactive = streamingInactive;
        callbacks_.frame_release = frameRelease;
        callbacks_.frame_slot_available = slotAvailable;
        APP_REQUIRE(UVCL_Init(USB1_OTG_HS, &config, &callbacks_) == 0);
        BSP_LED_On(LED_GREEN);
        std::printf("Nucleo camera: %dx%d H264, static C++20 pipeline\n", width, height);
    }

    static void frameInterrupt() {
        { InterruptGuard guard;
          if (!cameraActive_) return;
          completedGeneration_ = cameraGeneration_;
          completedTimestamp_ = HAL_GetTick();
        }
        notify(workerTask_, FrameCompleted);
    }
    static void vsyncInterrupt() { notify(workerTask_, IspUpdate); }
    static void errorInterrupt() {
        {
            InterruptGuard guard;
            cameraFault_ = true;
        }
        notify(controllerTask_, Fault);
    }

private:
    enum Event : std::uint32_t { Request = 1, Ready = 2, UsbAvailable = 4,
        Fault = 8, FrameCompleted = 16, IspUpdate = 32, Rearm = 64 };
    enum class State : std::uint8_t { Stopped, Starting, Streaming, Stopping, Faulted };
    struct RequestSnapshot { std::uint32_t generation; bool active; UVCL_StreamConf_t stream; };
    struct Statistics { std::uint32_t encoded, accepted, released, bytes,
        preEncodeDrops, encodeErrors, usbDrops, invalidReleases; };

    static inline TaskHandle_t controllerTask_{}, workerTask_{};
    static inline StaticTask_t controllerTcb_{}, workerTcb_{};
    static inline StackType_t controllerStack_[2048]{}, workerStack_[2048]{};
    static inline StaticSemaphore_t cameraMutexStorage_{};
    static inline SemaphoreHandle_t cameraMutex_{};
    static inline UVCL_Callbacks_t callbacks_{};
    static inline RequestSnapshot request_{};
    static inline camera::FramePool<bufferCount> capturePool_{}, outputPool_{};
    static inline bool cameraActive_{}, cameraFault_{};
    static inline bool encoderActive_{}, forceIntra_{true};
    static inline std::uint32_t cameraGeneration_{}, completedGeneration_{}, completedTimestamp_{};
    static inline std::array<std::uint32_t, bufferCount> captureTimes_{};
    static inline camera::FrameHandle capturing_{bufferCount, 0};
    static inline TickType_t captureStarted_{};
    static inline Statistics stats_{};
    static inline State state_{State::Stopped};

    static void notify(TaskHandle_t task, std::uint32_t events) {
        if (!task) return;
        if (__get_IPSR()) {
            BaseType_t awakened = pdFALSE;
            xTaskNotifyFromISR(task, events, eSetBits, &awakened);
            portYIELD_FROM_ISR(awakened);
        } else {
            xTaskNotify(task, events, eSetBits);
        }
    }

    static std::uint32_t wait(TickType_t timeout) {
        std::uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, timeout);
        return events;
    }

    static RequestSnapshot snapshot() {
        InterruptGuard guard;
        return request_;
    }

    static bool valid(std::uint32_t generation) {
        InterruptGuard guard;
        return request_.active && request_.generation == generation;
    }

    static bool faulted() {
        InterruptGuard guard;
        return cameraFault_;
    }

    static void streamingActive(UVCL_Callbacks_t *, UVCL_StreamConf_t stream) {
        {
            InterruptGuard guard;
            ++request_.generation;
            request_.active = true;
            request_.stream = stream;
        }
        BSP_LED_On(LED_RED);
        notify(controllerTask_, Request);
    }

    static void streamingInactive(UVCL_Callbacks_t *) {
        {
            InterruptGuard guard;
            ++request_.generation;
            request_.active = false;
        }
        BSP_LED_Off(LED_RED);
        notify(controllerTask_, Request);
    }

    static void slotAvailable(UVCL_Callbacks_t *) { notify(controllerTask_, UsbAvailable); }

    static void frameRelease(UVCL_Callbacks_t *, void *pointer) {
        /* USB owns accepted frames until the library detaches them. Failed
         * submissions never call this callback. Pool transitions cannot
         * corrupt unrelated entries on an unknown pointer or duplicate. */
        bool released = false;
        {
            InterruptGuard guard;
            for (std::size_t i = 0; i < bufferCount; ++i) {
                if (pointer == encodedStorage[i]) {
                    released = outputPool_.transition(outputPool_.handle(i),
                                camera::FrameState::UsbOwned, camera::FrameState::Free);
                    break;
                }
            }
            if (released) ++stats_.released;
            else ++stats_.invalidReleases;
        }
        notify(controllerTask_, UsbAvailable);
    }

    static bool start(const RequestSnapshot& request) {
        state_ = State::Starting;
        /* The board memory budget and advertised profile are deliberately
         * fixed. Reject host requests that would exceed capture or encoder
         * capacity before programming any DMA destination. */
        if (request.stream.payload_type != UVCL_PAYLOAD_FB_H264 ||
            request.stream.width != width || request.stream.height != height || request.stream.fps != fps)
            return false;
        ENC_Conf_t encoding{width, height, fps};
        const int result = ENC_Init(&encoding);
        if (result != 0) {
            std::printf("Encoder initialization failed: %d\n", result);
            return false;
        }
        encoderActive_ = true;
        CameraGuard lock(cameraMutex_);
        CAM_conf_t cameraConfig{width, height, fps, DCMIPP_PIXEL_PACKER_FORMAT_YUV422_1, 0};
        const int cameraResult = CAM_Init(&cameraConfig);
        if (cameraResult != CMW_ERROR_NONE) {
            std::printf("Camera initialization failed: %d\n", cameraResult);
            /* Best-effort rollback of an incomplete initialization. No DMA
             * transfer has started, and the next request probes the sensor. */
            CAM_Deinit();
            APP_REQUIRE(ENC_DeInit() == 0);
            encoderActive_ = false;
            return false;
        }
        {
            InterruptGuard guard;
            ++cameraGeneration_;
            cameraFault_ = false;
            capturing_ = {bufferCount, 0};
            cameraActive_ = true;
        }
        forceIntra_ = true;
        state_ = State::Streaming;
        notify(workerTask_, Rearm);
        return true;
    }

    static void stop() {
        state_ = State::Stopping;
        {
            InterruptGuard guard;
            cameraActive_ = false;
        }
        {
            CameraGuard lock(cameraMutex_);
            /* No camera worker can access peripherals once this lock is held.
             * A failed hardware stop is fatal: recycling DMA memory would be
             * unsafe while a bus master might still be writing to it. */
            APP_REQUIRE(CAM_Deinit() == CMW_ERROR_NONE);
            InterruptGuard guard;
            for (const auto from : {camera::FrameState::Capturing, camera::FrameState::Ready}) {
                for (;;) {
                    const auto handle = capturePool_.find(from);
                    if (handle.index == bufferCount) break;
                    APP_REQUIRE(capturePool_.transition(handle, from, camera::FrameState::Free));
                }
            }
            capturing_ = {bufferCount, 0};
        }
        if (encoderActive_) {
            APP_REQUIRE(ENC_DeInit() == 0);
            encoderActive_ = false;
        }
        /* Accepted output frames are not reset here: USB may still own them
         * during camera fault recovery. Their callback alone returns them. */
        state_ = State::Stopped;
    }

    static void cameraWorker(void *) {
        TickType_t timeout = portMAX_DELAY;
        for (;;) {
            const auto events = wait(timeout);
            std::uint32_t completedGeneration;
            { InterruptGuard guard; completedGeneration = completedGeneration_; }
            CameraGuard lock(cameraMutex_);
            bool active;
            { InterruptGuard guard; active = cameraActive_; }
            if (!active || faulted()) { timeout = portMAX_DELAY; continue; }
            if ((events & IspUpdate) && CAM_IspUpdate() != CMW_ERROR_NONE) {
                errorInterrupt();
                timeout = portMAX_DELAY;
                continue; // Release the lifecycle mutex before recovery.
            }
            if ((events & FrameCompleted) && completedGeneration == cameraGeneration_) {
                InterruptGuard guard;
                if (capturing_.index < bufferCount &&
                    capturePool_.transition(capturing_, camera::FrameState::Capturing, camera::FrameState::Ready)) {
                    captureTimes_[capturing_.index] = completedTimestamp_;
                    capturing_ = {bufferCount, 0};
                    notify(controllerTask_, Ready);
                }
            }
            if (capturing_.index < bufferCount) {
                if (xTaskGetTickCount() - captureStarted_ >= pdMS_TO_TICKS(200))
                    errorInterrupt();
                timeout = pdMS_TO_TICKS(200);
                continue;
            }
            {
                InterruptGuard guard;
                capturing_ = capturePool_.acquire(camera::FrameState::Capturing, cameraGeneration_);
            }
            if (capturing_.index == bufferCount) {
                timeout = portMAX_DELAY;
                continue;
            }
            /* Snapshot mode guarantees DCMIPP stops writing before a buffer
             * is published Ready. The sensor keeps streaming, but capture is
             * rearmed only into a free buffer; USB backpressure cannot cause
             * the CPU encoder to read a frame being overwritten by DMA. */
            captureStarted_ = xTaskGetTickCount();
            if (CAM_CapturePipe_Start(captureStorage[capturing_.index], CMW_MODE_SNAPSHOT) != CMW_ERROR_NONE)
                errorInterrupt();
            timeout = pdMS_TO_TICKS(200);
        }
    }

    static void returnCapture(camera::FrameHandle handle) {
        {
            InterruptGuard guard;
            APP_REQUIRE(capturePool_.transition(handle, camera::FrameState::Encoding, camera::FrameState::Free));
        }
        notify(workerTask_, Rearm);
    }

    static bool awaitUsb(std::uint32_t generation, TickType_t began) {
        const auto budget = pdMS_TO_TICKS(33);
        while (valid(generation) && !faulted()) {
            if (UVCL_CanAcceptFrame()) return true;
            const auto elapsed = xTaskGetTickCount() - began;
            if (elapsed >= budget) return false;
            wait(budget - elapsed);
        }
        return false;
    }

    static void send(camera::FrameHandle capture, std::uint32_t generation) {
        camera::FrameHandle output;
        {
            InterruptGuard guard;
            output = outputPool_.acquire(camera::FrameState::Encoding, generation);
        }
        const auto began = xTaskGetTickCount();
        if (output.index == bufferCount || !awaitUsb(generation, began)) {
            if (output.index < bufferCount) {
                InterruptGuard guard;
                APP_REQUIRE(outputPool_.transition(output, camera::FrameState::Encoding, camera::FrameState::Free));
            }
            ++stats_.preEncodeDrops;
            /* A pre-encode drop does not change H264 reference frames and
             * therefore does not require an unnecessarily large IDR. */
            returnCapture(capture);
            return;
        }
        const int length = ENC_EncodeFrame(captureStorage[capture.index],
                                          encodedStorage[output.index], H264_OUT_BUFFER_SIZE, forceIntra_, captureTimes_[capture.index]);
        returnCapture(capture);
        if (length <= 0) {
            std::printf("Encode failed: %d\n", length);
            forceIntra_ = true;
            ++stats_.encodeErrors;
            InterruptGuard guard;
            APP_REQUIRE(outputPool_.transition(output, camera::FrameState::Encoding, camera::FrameState::Free));
            return;
        }
        forceIntra_ = false;
        ++stats_.encoded;
        stats_.bytes += static_cast<std::uint32_t>(length);
        for (;;) {
            if (!valid(generation) || faulted()) break;
            {
                InterruptGuard guard;
                APP_REQUIRE(outputPool_.transition(output, camera::FrameState::Encoding, camera::FrameState::UsbOwned));
            }
            if (UVCL_ShowFrame(encodedStorage[output.index], length) == 0) {
                ++stats_.accepted;
                return;
            }
            {
                InterruptGuard guard;
                APP_REQUIRE(outputPool_.transition(output, camera::FrameState::UsbOwned, camera::FrameState::Encoding));
            }
            if (!awaitUsb(generation, began)) break;
        }
        ++stats_.usbDrops;
        /* An encoded-but-unsent frame changed the encoder's references. The
         * next frame must be intra so the host decoder can recover. */
        forceIntra_ = true;
        InterruptGuard guard;
        APP_REQUIRE(outputPool_.transition(output, camera::FrameState::Encoding, camera::FrameState::Free));
    }

    static void run(void *) {
        std::uint32_t previousGeneration = 0;
        unsigned failures = 0;
        for (;;) {
            const auto request = snapshot();
            if (!request.active) { wait(portMAX_DELAY); continue; }
            if (request.generation != previousGeneration) { failures = 0; previousGeneration = request.generation; }
            if (failures >= 3) { state_ = State::Faulted; wait(portMAX_DELAY); continue; }
            if (!start(request)) { ++failures; wait(pdMS_TO_TICKS(250)); continue; }
            std::printf("Start stream generation %lu\n", static_cast<unsigned long>(request.generation));
            auto lastTime = xTaskGetTickCount();
            Statistics previous{};
            { InterruptGuard guard; previous = stats_; }
            while (valid(request.generation) && !faulted()) {
                camera::FrameHandle frame;
                {
                    InterruptGuard guard;
                    frame = capturePool_.find(camera::FrameState::Ready);
                    if (frame.index < bufferCount)
                        APP_REQUIRE(capturePool_.transition(frame, camera::FrameState::Ready, camera::FrameState::Encoding));
                }
                if (frame.index < bufferCount) send(frame, request.generation);
                else wait(pdMS_TO_TICKS(200));
                const auto now = xTaskGetTickCount();
                const auto elapsed = now - lastTime;
                if (elapsed >= pdMS_TO_TICKS(1000)) {
                    Statistics current;
                    { InterruptGuard guard; current = stats_; }
                    /* Normalize rates to the measured interval. Encoded,
                     * accepted and released counts describe different stages. */
                    std::printf("enc_fps=%lu usb_staged_fps=%lu enc_kbps=%lu pre_drop=%lu enc_err=%lu usb_drop=%lu arena_peak=%lu stream_stack_free=%lu camera_stack_free=%lu\n",
                        static_cast<unsigned long>(static_cast<std::uint64_t>(current.encoded - previous.encoded) * configTICK_RATE_HZ / elapsed),
                        static_cast<unsigned long>(static_cast<std::uint64_t>(current.released - previous.released) * configTICK_RATE_HZ / elapsed),
                        static_cast<unsigned long>(static_cast<std::uint64_t>(current.bytes - previous.bytes) * 8U * configTICK_RATE_HZ / (elapsed * 1000ULL)),
                        static_cast<unsigned long>(current.preEncodeDrops), static_cast<unsigned long>(current.encodeErrors),
                        static_cast<unsigned long>(current.usbDrops),
                        static_cast<unsigned long>(ENC_GetArenaHighWater()),
                        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(controllerTask_) * sizeof(StackType_t)),
                        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(workerTask_) * sizeof(StackType_t)));
                    previous = current;
                    lastTime = now;
                }
            }
            const bool recover = faulted() && valid(request.generation);
            stop();
            if (recover) { ++failures; wait(pdMS_TO_TICKS(250)); }
        }
    }
};
} // namespace

extern "C" void app_run(void) { StreamController::initialize(); }
extern "C" void App_CameraErrorFromISR(void) { StreamController::errorInterrupt(); }
extern "C" int CMW_CAMERA_PIPE_FrameEventCallback(std::uint32_t pipe) {
    if (pipe == DCMIPP_PIPE1) StreamController::frameInterrupt();
    return HAL_OK;
}
extern "C" int CMW_CAMERA_PIPE_VsyncEventCallback(std::uint32_t pipe) {
    if (pipe == DCMIPP_PIPE1) StreamController::vsyncInterrupt();
    return HAL_OK;
}
