#include <stdbool.h>
#include <stdint.h>
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
    }

    return true;
}

static bool test_web888_memory_budget(void)
{
    printf("web-888 FST4W memory model (2 monitor frames, 128 MiB reserve):\n");

    for (int i = 0; i < FST4_NUM_TR_PERIODS; ++i)
    {
        int period = kFST4_TR_periods[i];
        monitor_config_t cfg = make_config(FTX_PROTOCOL_FST4W, (float)period);
        monitor_memory_usage_t usage;
        CHECK(monitor_get_memory_usage(&cfg, &usage));

        size_t sample_bytes = (size_t)period * (size_t)cfg.sample_rate * sizeof(float);
        size_t per_channel = sample_bytes + WEB888_FRAME_COUNT * usage.total_bytes;
        size_t available = WEB888_MEMORY_BUDGET - WEB888_MEMORY_RESERVE;
        size_t max_channels = available / per_channel;

        printf("  %4ds: %6.1f MiB/channel, at most %zu channel(s)\n",
            period, per_channel / (double)MIB, max_channels);

        CHECK(per_channel < WEB888_MEMORY_BUDGET);
        CHECK(max_channels >= 1);
        if (period == 1800)
            CHECK(max_channels <= 2);
    }

    return true;
}

static bool test_web888_monitor_lifecycle(void)
{
    monitor_config_t cfg = make_config(FTX_PROTOCOL_FST4W, 15);
    monitor_t frames[WEB888_FRAME_COUNT];
    memset(frames, 0, sizeof(frames));

    for (size_t i = 0; i < WEB888_FRAME_COUNT; ++i)
    {
        CHECK(monitor_init(&frames[i], &cfg));
        CHECK(frames[i].wf.desc != NULL);
        CHECK(frames[i].wf.desc->protocol == FTX_PROTOCOL_FST4W);
        CHECK(frames[i].wf.mag != NULL);
        CHECK(frames[i].timedata != NULL);
        CHECK(frames[i].freqdata != NULL);
    }

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
        CHECK(frames[i].timedata == NULL);
        CHECK(frames[i].freqdata == NULL);
        monitor_free(&frames[i]);
    }

    return true;
}

int main(void)
{
    CHECK(test_invalid_configs());
    CHECK(test_fst4w_period_memory());
    CHECK(test_web888_memory_budget());
    CHECK(test_web888_monitor_lifecycle());
    printf("Monitor integration tests OK\n");
    return 0;
}
