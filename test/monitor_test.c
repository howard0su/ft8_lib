#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/monitor.h"
#include "ft8/constants.h"

#define MIB (1024u * 1024u)
#define WEB888_FRAME_COUNT 2u
#define WEB888_MEMORY_BUDGET (512u * MIB)
#define WEB888_MEMORY_RESERVE (128u * MIB)

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                       \
        }                                                                       \
    } while (0)

static monitor_config_t make_config(ftx_protocol_t protocol, float tr_period)
{
    monitor_config_t cfg = {
        .f_min = 100,
        .f_max = 3100,
        .sample_rate = 12000,
        .time_osr = 4,
        .freq_osr = 2,
        .protocol = protocol,
        .tr_period = tr_period
    };
    return cfg;
}

static monitor_config_t make_web888_fst4w_config(float tr_period)
{
    monitor_config_t cfg = {
        .f_min = 600,
        .f_max = 900,
        .sample_rate = 6000,
        .time_osr = 4,
        .freq_osr = 2,
        .protocol = FTX_PROTOCOL_FST4W,
        .tr_period = tr_period
    };
    return cfg;
}

static bool test_invalid_configs(void)
{
    monitor_memory_usage_t usage;
    monitor_config_t cfg = make_config(FTX_PROTOCOL_FST4W, 60);

    CHECK(monitor_get_memory_usage(&cfg, &usage));

    cfg.tr_period = 45;
    CHECK(!monitor_get_memory_usage(&cfg, &usage));
    cfg.tr_period = 60.5f;
    CHECK(!monitor_get_memory_usage(&cfg, &usage));

    cfg = make_config((ftx_protocol_t)99, 0);
    CHECK(!monitor_get_memory_usage(&cfg, &usage));

    cfg = make_config(FTX_PROTOCOL_FT8, 0);
    cfg.f_max = 6100;
    CHECK(!monitor_get_memory_usage(&cfg, &usage));

    cfg = make_config(FTX_PROTOCOL_FT8, 0);
    cfg.time_osr = 0;
    CHECK(!monitor_get_memory_usage(&cfg, &usage));

    return true;
}

static bool test_fst4w_period_memory(void)
{
    size_t previous_total = 0;

    for (int i = 0; i < FST4_NUM_TR_PERIODS; ++i)
    {
        monitor_config_t cfg = make_config(FTX_PROTOCOL_FST4W, (float)kFST4_TR_periods[i]);
        monitor_memory_usage_t usage;
        CHECK(monitor_get_memory_usage(&cfg, &usage));
        CHECK(usage.waterfall_bytes > 0);
        CHECK(usage.timedata_bytes > 0);
        CHECK(usage.freqdata_bytes > 0);
        CHECK(usage.total_bytes > previous_total);
        previous_total = usage.total_bytes;

        monitor_t mon;
        CHECK(monitor_init(&mon, &cfg));
        CHECK(mon.block_size == kFST4_NSPS[i]);
        CHECK(mon.shared->subblock_size * cfg.time_osr == mon.block_size);
        monitor_free(&mon);
    }

    return true;
}

static bool test_web888_memory_budget(void)
{
    printf("web-888 FST4W memory model (shared DSP, 2 frames, streaming 12k->6k):\n");

    for (int i = 0; i < FST4_NUM_TR_PERIODS; ++i)
    {
        int period = kFST4_TR_periods[i];
        monitor_config_t cfg = make_web888_fst4w_config((float)period);
        monitor_memory_usage_t usage;
        CHECK(monitor_get_memory_usage(&cfg, &usage));

        size_t stream_bytes = (size_t)lround(cfg.sample_rate * ((float)kFST4_NSPS[i] / 12000.0f)) * sizeof(float);
        size_t per_channel = stream_bytes + usage.shared_bytes + WEB888_FRAME_COUNT * usage.frame_bytes;
        size_t available = WEB888_MEMORY_BUDGET - WEB888_MEMORY_RESERVE;
        size_t max_channels = available / per_channel;

        printf("  %4ds: %6.1f MiB/channel, at most %zu channel(s)\n",
            period, per_channel / (double)MIB, max_channels);

        CHECK(per_channel < WEB888_MEMORY_BUDGET);
        CHECK(max_channels >= 1);
        if (period == 1800)
            CHECK(per_channel <= 12u * MIB);
    }

    return true;
}

static bool test_streaming_decimation(void)
{
    monitor_config_t cfg = make_web888_fst4w_config(15);
    monitor_shared_t direct_shared;
    monitor_shared_t stream_shared;
    monitor_t direct_frame;
    monitor_t stream_frame;
    monitor_stream_t stream;

    CHECK(monitor_shared_init(&direct_shared, &cfg));
    CHECK(monitor_shared_init(&stream_shared, &cfg));
    CHECK(monitor_frame_init(&direct_frame, &direct_shared));
    CHECK(monitor_frame_init(&stream_frame, &stream_shared));
    CHECK(monitor_stream_init(&stream, &stream_frame, 12000));
    CHECK(stream.decimation == 2);

    int input_count = direct_frame.block_size * stream.decimation;
    int16_t* input = (int16_t*)malloc((size_t)input_count * sizeof(input[0]));
    float* direct = (float*)malloc((size_t)direct_frame.block_size * sizeof(direct[0]));
    CHECK(input != NULL);
    CHECK(direct != NULL);

    for (int i = 0; i < input_count; ++i)
        input[i] = (int16_t)lround(16000.0 * sin(2.0 * M_PI * 750.0 * i / 12000.0));
    for (int i = 0; i < direct_frame.block_size; ++i)
        direct[i] = input[i * stream.decimation] / 32768.0f;

    monitor_process(&direct_frame, direct);
    int split = input_count / 3;
    CHECK(monitor_stream_process_i16(&stream, input, split) == 0);
    CHECK(monitor_stream_process_i16(&stream, input + split, input_count - split) == 1);
    CHECK(direct_frame.wf.num_blocks == 1);
    CHECK(stream_frame.wf.num_blocks == 1);
    CHECK(memcmp(direct_frame.wf.mag, stream_frame.wf.mag,
        (size_t)direct_frame.wf.block_stride * sizeof(direct_frame.wf.mag[0])) == 0);

    free(direct);
    free(input);
    monitor_stream_free(&stream);
    monitor_free(&stream_frame);
    monitor_free(&direct_frame);
    monitor_shared_free(&stream_shared);
    monitor_shared_free(&direct_shared);
    return true;
}

static bool test_web888_monitor_lifecycle(void)
{
    monitor_config_t cfg = make_config(FTX_PROTOCOL_FST4W, 15);
    monitor_shared_t shared;
    monitor_t frames[WEB888_FRAME_COUNT];
    memset(frames, 0, sizeof(frames));

    CHECK(monitor_shared_init(&shared, &cfg));
    for (size_t i = 0; i < WEB888_FRAME_COUNT; ++i)
    {
        CHECK(monitor_frame_init(&frames[i], &shared));
        CHECK(frames[i].wf.desc != NULL);
        CHECK(frames[i].wf.desc->protocol == FTX_PROTOCOL_FST4W);
        CHECK(frames[i].wf.mag != NULL);
        CHECK(frames[i].shared == &shared);
    }
    CHECK(shared.timedata != NULL);
    CHECK(shared.freqdata != NULL);

    float* samples = (float*)calloc((size_t)frames[0].block_size, sizeof(float));
    CHECK(samples != NULL);
    monitor_process(&frames[0], samples);
    CHECK(frames[0].wf.num_blocks == 1);
    monitor_reset(&frames[0]);
    CHECK(frames[0].wf.num_blocks == 0);
    free(samples);

    for (size_t i = 0; i < WEB888_FRAME_COUNT; ++i)
    {
        monitor_free(&frames[i]);
        CHECK(frames[i].wf.mag == NULL);
        CHECK(frames[i].shared == NULL);
        monitor_free(&frames[i]);
    }
    monitor_shared_free(&shared);
    CHECK(shared.timedata == NULL);
    CHECK(shared.freqdata == NULL);

    return true;
}

int main(void)
{
    CHECK(test_invalid_configs());
    CHECK(test_fst4w_period_memory());
    CHECK(test_web888_memory_budget());
    CHECK(test_web888_monitor_lifecycle());
    CHECK(test_streaming_decimation());
    printf("Monitor integration tests OK\n");
    return 0;
}
