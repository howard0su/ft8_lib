//
// LDPC encoder/decoder for FST4 and FST4W.
//
// FST4 uses (240,101) LDPC code: 101 info bits, 139 parity bits, 240 total.
// FST4W uses (240,74) LDPC code: 74 info bits, 166 parity bits, 240 total.
// Both codes have column weight 3 (each bit participates in exactly 3 checks).
//
// The BP decoder follows the same sum-product algorithm as the FT8 decoder
// in ldpc.c, adapted for the different code dimensions.
//
// Note: FST4 Mn/Nm arrays are 0-indexed (unlike FT8's 1-indexed arrays).
//

#include "fst4_ldpc.h"
#include "constants.h"

#include <math.h>
#include <stdbool.h>

static uint8_t parity8(uint8_t x)
{
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1;
}

static float fast_tanh(float x)
{
    if (x < -4.97f)
        return -1.0f;
    if (x > 4.97f)
        return 1.0f;
    float x2 = x * x;
    float a = x * (945.0f + x2 * (105.0f + x2));
    float b = 945.0f + x2 * (420.0f + x2 * 15.0f);
    return a / b;
}

static float fast_atanh(float x)
{
    float x2 = x * x;
    float a = x * (945.0f + x2 * (-735.0f + x2 * 64.0f));
    float b = (945.0f + x2 * (-1050.0f + x2 * 225.0f));
    return a / b;
}

// Generic bitpacked LDPC encoder: info_bits → codeword using generator matrix
static void encode240(const uint8_t* message, uint8_t* codeword,
                      const uint8_t* generator, int k, int k_bytes, int m)
{
    int n_bytes = (k + m + 7) / 8;

    // Copy message bits into codeword, zero-fill the rest
    for (int j = 0; j < n_bytes; ++j)
    {
        codeword[j] = (j < k_bytes) ? message[j] : 0;
    }

    // Compute parity bits using the generator matrix
    uint8_t col_mask = (0x80u >> (k % 8u));
    uint8_t col_idx = k_bytes - 1;

    for (int i = 0; i < m; ++i)
    {
        const uint8_t* gen_row = generator + (i * k_bytes);
        uint8_t nsum = 0;
        for (int j = 0; j < k_bytes; ++j)
        {
            nsum ^= parity8(message[j] & gen_row[j]);
        }

        if (nsum & 1)
        {
            codeword[col_idx] |= col_mask;
        }

        col_mask >>= 1;
        if (col_mask == 0)
        {
            col_mask = 0x80u;
            ++col_idx;
        }
    }
}

void fst4_ldpc_encode(const uint8_t* message, uint8_t* codeword)
{
    encode240(message, codeword,
              (const uint8_t*)kFST4_LDPC_generator,
              FST4_LDPC_K, FST4_LDPC_K_BYTES, FST4_LDPC_M);
}

void fst4w_ldpc_encode(const uint8_t* message, uint8_t* codeword)
{
    encode240(message, codeword,
              (const uint8_t*)kFST4W_LDPC_generator,
              FST4W_LDPC_K, FST4W_LDPC_K_BYTES, FST4W_LDPC_M);
}

// Generic BP decoder for (240,K) LDPC codes
// Mn/Nm arrays are 0-indexed.
static void bp_decode_240(const float codeword[], int max_iters,
                          uint8_t plain[], int* ok,
                          int n, int m, int max_nrw,
                          const uint16_t* mn,     // [n][3]
                          const uint16_t* nm,     // [m][max_nrw]
                          const uint8_t* num_rows) // [m]
{
    float tov[FST4_LDPC_N][3];
    float toc[FST4W_LDPC_M][FST4_LDPC_MAX_NRW]; // Use max dimensions

    int min_errors = m;

    for (int i = 0; i < n; ++i)
    {
        tov[i][0] = tov[i][1] = tov[i][2] = 0;
    }

    for (int iter = 0; iter < max_iters; ++iter)
    {
        // Hard decision
        int plain_sum = 0;
        for (int i = 0; i < n; ++i)
        {
            plain[i] = ((codeword[i] + tov[i][0] + tov[i][1] + tov[i][2]) > 0) ? 1 : 0;
            plain_sum += plain[i];
        }

        if (plain_sum == 0)
        {
            min_errors = m;
            break;
        }

        // Parity check
        int errors = 0;
        for (int j = 0; j < m; ++j)
        {
            uint8_t x = 0;
            int nrw = num_rows[j];
            for (int i = 0; i < nrw; ++i)
            {
                x ^= plain[nm[j * max_nrw + i]];
            }
            if (x != 0)
                ++errors;
        }

        if (errors < min_errors)
        {
            min_errors = errors;
            if (errors == 0)
                break;
        }

        // Messages from bits to check nodes
        for (int j = 0; j < m; ++j)
        {
            int nrw = num_rows[j];
            for (int n_idx = 0; n_idx < nrw; ++n_idx)
            {
                int ni = nm[j * max_nrw + n_idx];
                float Tnm = codeword[ni];
                for (int m_idx = 0; m_idx < 3; ++m_idx)
                {
                    if (mn[ni * 3 + m_idx] != (uint16_t)j)
                    {
                        Tnm += tov[ni][m_idx];
                    }
                }
                toc[j][n_idx] = fast_tanh(-Tnm / 2);
            }
        }

        // Messages from check nodes to variable nodes
        for (int i = 0; i < n; ++i)
        {
            for (int m_idx = 0; m_idx < 3; ++m_idx)
            {
                int j = mn[i * 3 + m_idx];
                float Tmn = 1.0f;
                int nrw = num_rows[j];
                for (int n_idx = 0; n_idx < nrw; ++n_idx)
                {
                    if (nm[j * max_nrw + n_idx] != (uint16_t)i)
                    {
                        Tmn *= toc[j][n_idx];
                    }
                }
                tov[i][m_idx] = -2 * fast_atanh(Tmn);
            }
        }
    }

    *ok = min_errors;
}

void fst4_ldpc_decode(const float codeword[], int max_iters, uint8_t plain[], int* ok)
{
    bp_decode_240(codeword, max_iters, plain, ok,
                  FST4_LDPC_N, FST4_LDPC_M, FST4_LDPC_MAX_NRW,
                  (const uint16_t*)kFST4_LDPC_Mn,
                  (const uint16_t*)kFST4_LDPC_Nm,
                  kFST4_LDPC_num_rows);
}

void fst4w_ldpc_decode(const float codeword[], int max_iters, uint8_t plain[], int* ok)
{
    bp_decode_240(codeword, max_iters, plain, ok,
                  FST4_LDPC_N, FST4W_LDPC_M, FST4W_LDPC_MAX_NRW,
                  (const uint16_t*)kFST4W_LDPC_Mn,
                  (const uint16_t*)kFST4W_LDPC_Nm,
                  kFST4W_LDPC_num_rows);
}
