#include "uvcl_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "Check failed at %d: %s\n", __LINE__, #expr); abort(); } } while (0)
extern UVCL_Ctx_t *p_ctx_single;
static unsigned releases, stops, slots;
static void *released[8];
static void release(UVCL_Callbacks_t *callbacks, void *frame) {
    (void)callbacks;
    CHECK(releases < 8);
    released[releases++] = frame;
}
static void stop(UVCL_Callbacks_t *callbacks) { (void)callbacks; ++stops; }
static void slot(UVCL_Callbacks_t *callbacks) { (void)callbacks; ++slots; }
uint32_t HAL_GetTick(void) { return 0; }
int main(void) {
    UVCL_Ctx_t context = {0};
    UVCL_Callbacks_t callbacks = {0};
    callbacks.frame_release = release;
    callbacks.streaming_inactive = stop;
    callbacks.frame_slot_available = slot;
    context.cbs = &callbacks;
    context.state = UVCL_STATUS_STREAMING;
    context.conf.is_immediate_mode = 1;
    uint8_t packet[3072], first[65536], second[64];
    context.packet = packet;
    memset(packet, 0, sizeof(packet));
    p_ctx_single = &context;
    CHECK(UVCL_ShowFrame(first, 3069) == 0);
    context.on_fly_ctx = UVCL_StartNewFrameTransmission(&context, sizeof(packet));
    CHECK(context.on_fly_ctx && context.on_fly_ctx->packet_nb == 1);
    CHECK(UVCL_ShowFrame(first, 3069) != 0); /* Active pointer cannot be queued twice. */
    CHECK(UVCL_ShowFrame(second, sizeof(second)) == 0);
    UVCL_StopContext(&context);
    CHECK(releases == 2 && released[0] == first && released[1] == second);
    UVCL_StopContext(&context);
    CHECK(releases == 2); /* Repeated stop does not release twice. */
    CHECK(UVCL_ShowFrame(first, 32) != 0);
    CHECK(releases == 2);
    context.state = UVCL_STATUS_STREAMING;
    CHECK(UVCL_ShowFrame(first, 65536) == 0);
    context.on_fly_ctx = UVCL_StartNewFrameTransmission(&context, sizeof(packet));
    UVCL_UpdateOnFlyCtx(&context, sizeof(packet));
    CHECK(context.on_fly_ctx->packet_index == 1);
    UVCL_AbortOnFlyCtx(&context);
    CHECK(releases == 3);
    CHECK(UVCL_ShowFrame(second, sizeof(second)) == 0);
    context.on_fly_ctx = UVCL_StartNewFrameTransmission(&context, sizeof(packet));
    CHECK(context.on_fly_ctx->packet_index == 0 && context.on_fly_ctx->packet_nb == 1);
    UVCL_UpdateOnFlyCtx(&context, sizeof(second) + 2);
    CHECK(releases == 4 && released[3] == second && !context.on_fly_ctx);
    CHECK(slots == 3 && stops == 2);
    CHECK(UVCL_ShowFrame(first, -1) != 0);
    CHECK(UVCL_ComputedwMaxVideoFrameSize(&context, 255, 255) == 0);
    UVCL_StreamConf_t configuration;
    CHECK(UVCL_SetupStreamingStream(&context, &configuration) != 0);
    puts("Passed actual UVCL submit/stop, duplicate, abort/restart and invalid request paths");
}
