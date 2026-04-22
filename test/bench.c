/*
 * Benchmark harness for LDPC/OSD decoders.
 * Measures µs/call for bp_decode, osd_decode, fst4_ldpc_decode.
 *
 * Build with: make bench  (uses -O3, no ASAN)
 * Usage: ./bench
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#include "ft8/ldpc.h"
#include "ft8/osd.h"
#include "ft8/encode.h"
#include "ft8/constants.h"
#include "ft8/fst4_ldpc.h"

// Simple LCG PRNG for reproducible noise
static uint32_t rng_state = 12345;
static float rng_uniform(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return (float)(rng_state >> 16) / 65536.0f;
}

// Box-Muller for Gaussian noise
static float rng_gaussian(void)
{
    float u1 = rng_uniform();
    float u2 = rng_uniform();
    if (u1 < 1e-10f)
        u1 = 1e-10f;
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * 3.14159265f * u2);
}

static double now_us(void)
{
#ifdef __APPLE__
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0)
        mach_timebase_info(&tb);
    uint64_t t = mach_absolute_time();
    return (double)t * tb.numer / tb.denom / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1000.0;
#endif
}

// Generate LLRs from a known codeword with additive Gaussian noise
// LLR convention: positive = more likely 0
static void make_noisy_llr(const uint8_t* codeword_bits, int n, float snr_db, float* llr)
{
    float sigma = powf(10.0f, -snr_db / 20.0f);
    for (int i = 0; i < n; i++)
    {
        float signal = codeword_bits[i] ? -1.0f : 1.0f;
        float noise = sigma * rng_gaussian();
        float received = signal + noise;
        // LLR = 2*received/sigma^2
        llr[i] = 2.0f * received / (sigma * sigma);
    }
}

static void bench_bp_decode(int n_runs)
{
    // Create a valid FT8 codeword
    uint8_t payload[FTX_LDPC_K_BYTES];
    memset(payload, 0, sizeof(payload));
    payload[0] = 0x12;
    payload[1] = 0x34;
    payload[2] = 0x56;

    uint8_t tones[FT8_NN];
    ft8_encode(payload, tones);

    // Encode to get the valid codeword bits
    uint8_t plain91[FTX_LDPC_K];
    for (int i = 0; i < FTX_LDPC_K; i++)
        plain91[i] = (payload[i / 8] >> (7 - (i % 8))) & 1;

    uint8_t codeword_bits[FTX_LDPC_N];
    ldpc_encode(plain91, codeword_bits);

    float llr[FTX_LDPC_N];
    uint8_t decoded[FTX_LDPC_N];
    int ok;

    // Warm up
    make_noisy_llr(codeword_bits, FTX_LDPC_N, 5.0f, llr);
    bp_decode(llr, 25, decoded, &ok);

    // Benchmark
    int successes = 0;
    double t0 = now_us();
    for (int i = 0; i < n_runs; i++)
    {
        rng_state = 12345 + i;
        make_noisy_llr(codeword_bits, FTX_LDPC_N, 3.0f, llr);
        bp_decode(llr, 25, decoded, &ok);
        if (ok == 0)
            successes++;
    }
    double elapsed = now_us() - t0;

    printf("bp_decode (174-bit, 25 iters):  %8.1f µs/call  (%d/%d decoded)  [%d runs]\n",
           elapsed / n_runs, successes, n_runs, n_runs);
}

static void bench_osd_decode(int n_runs)
{
    uint8_t payload[FTX_LDPC_K_BYTES];
    memset(payload, 0, sizeof(payload));
    payload[0] = 0xAB;
    payload[1] = 0xCD;

    uint8_t plain91[FTX_LDPC_K];
    for (int i = 0; i < FTX_LDPC_K; i++)
        plain91[i] = (payload[i / 8] >> (7 - (i % 8))) & 1;

    uint8_t codeword_bits[FTX_LDPC_N];
    ldpc_encode(plain91, codeword_bits);

    float llr[FTX_LDPC_N];
    uint8_t decoded[FTX_LDPC_N];
    int ok;

    // Warm up
    make_noisy_llr(codeword_bits, FTX_LDPC_N, 0.0f, llr);
    osd_decode(llr, 6, decoded, &ok);

    // Benchmark — use low SNR so OSD actually does work
    int successes = 0;
    double t0 = now_us();
    for (int i = 0; i < n_runs; i++)
    {
        rng_state = 54321 + i;
        make_noisy_llr(codeword_bits, FTX_LDPC_N, 0.0f, llr);
        osd_decode(llr, 6, decoded, &ok);
        if (ok == 0)
            successes++;
    }
    double elapsed = now_us() - t0;

    printf("osd_decode (174-bit, depth 6):  %8.1f µs/call  (%d/%d decoded)  [%d runs]\n",
           elapsed / n_runs, successes, n_runs, n_runs);
}

static void bench_fst4_ldpc_decode(int n_runs)
{
    // Create a valid FST4 codeword (101-bit message → 240-bit LDPC)
    uint8_t message[13]; // 101 bits = 13 bytes
    memset(message, 0, sizeof(message));
    message[0] = 0x55;
    message[1] = 0xAA;
    message[2] = 0x33;

    uint8_t codeword_packed[30]; // 240 bits = 30 bytes
    fst4_ldpc_encode(message, codeword_packed);

    // Unpack to bits for LLR generation
    uint8_t codeword_bits[240];
    for (int i = 0; i < 240; i++)
        codeword_bits[i] = (codeword_packed[i / 8] >> (7 - (i % 8))) & 1;

    float llr[240];
    uint8_t decoded[240];
    int ok;

    // Warm up
    make_noisy_llr(codeword_bits, 240, 5.0f, llr);
    fst4_ldpc_decode(llr, 100, decoded, &ok);

    // Benchmark
    int successes = 0;
    double t0 = now_us();
    for (int i = 0; i < n_runs; i++)
    {
        rng_state = 99999 + i;
        make_noisy_llr(codeword_bits, 240, 3.0f, llr);
        fst4_ldpc_decode(llr, 100, decoded, &ok);
        if (ok == 0)
            successes++;
    }
    double elapsed = now_us() - t0;

    printf("fst4_ldpc_decode (240-bit, 100 iters): %8.1f µs/call  (%d/%d decoded)  [%d runs]\n",
           elapsed / n_runs, successes, n_runs, n_runs);
}

int main(int argc, char* argv[])
{
    int bp_runs = 10000;
    int osd_runs = 1000;
    int fst4_runs = 10000;

    if (argc > 1)
    {
        int scale = atoi(argv[1]);
        if (scale > 0)
        {
            bp_runs = scale;
            osd_runs = scale / 10;
            if (osd_runs < 100)
                osd_runs = 100;
            fst4_runs = scale;
        }
    }

    printf("=== LDPC/OSD Benchmark ===\n\n");

    bench_bp_decode(bp_runs);
    bench_osd_decode(osd_runs);
    bench_fst4_ldpc_decode(fst4_runs);

    printf("\nDone.\n");
    return 0;
}
