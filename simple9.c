/*
Copyright 2012 Christopher Hoobin. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided
with the distribution.

THIS SOFTWARE IS PROVIDED BY CHRISTOPHER HOOBIN ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL CHRISTOPHER HOOBIN OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation
are those of the authors and should not be interpreted as representing
official policies, either expressed or implied, of Christopher Hoobin.
*/

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>

#define UINT32_BITS (sizeof(uint32_t) * CHAR_BIT)

#define SELECTOR_MASK 0x0000000F

#define SELECTOR_BITS 4

#define CODE_BITS (UINT32_BITS - SELECTOR_BITS)

#define MAX_VALUE ((1UL << CODE_BITS) - 1)

#define NSELECTORS 9

static const struct {
    uint32_t nitems;
    uint32_t nbits;
    uint32_t nwaste;
} selectors[NSELECTORS] = {
    {28,  1, 0},
    {14,  2, 0},
    { 9,  3, 1},
    { 7,  4, 0},
    { 5,  5, 3},
    { 4,  7, 0},
    { 3,  9, 1},
    { 2, 14, 0},
    { 1, 28, 0},
};

#if defined NDEBUG
#define ___IO(operation, value, size, n, fp) do {   \
        f##operation(value, size, n, fp);           \
    } while(0)
#else
#define ___IO(operation, value, size, n, fp) do {           \
        size_t nitems = f##operation(value, size, n, fp);   \
        assert(nitems == n);                                \
    } while(0)
#endif

#define Fread(value, size, n, fp) ___IO(read, value, size, n, fp)

#define Fwrite(value, size, n, fp) ___IO(write, value, size, n, fp)

static size_t vbyte_encode(size_t value, FILE *fp)
{
    size_t nbytes = 0;
    uint8_t nibble;

    while (value >= 0x80) {
        nibble = (value & 0x7F) | 0x80;

        Fwrite(&nibble, sizeof nibble, 1, fp);
        nbytes++;

        value >>= 7;
    }

    nibble = value & 0x7F;

    Fwrite(&nibble, sizeof nibble, 1, fp);
    nbytes++;

    return nbytes;
}

static size_t vbyte_decode(size_t *value, FILE *fp)
{
    size_t nbytes = 0;
    size_t shift = 0;
    uint8_t nibble;

    *value = 0;

    while (1) {
        Fread(&nibble, sizeof nibble, 1, fp);
        nbytes++;

        *value |= ((nibble & 0x7F) << shift);

        shift += 7;

        if (nibble < 0x80)
            break;
    }

    return nbytes;
}

size_t simple9_encode(uint32_t *array, size_t n, FILE *fp)
{
    uint32_t index;
    uint32_t selector;
    uint32_t data;
    uint32_t shift;

    size_t nbytes;
    size_t nitems;
    size_t i;

    assert(array);
    assert(n > 0);
    assert(fp);

    nbytes = vbyte_encode(n, fp);

    index = 0;

    while (index < n) {
        for (selector = 0; selector < NSELECTORS; selector++) {
            data = selector;
            shift = SELECTOR_BITS;
            nitems = 0;

            for (i = index; i < n; i++) {
                assert(array[i] <= MAX_VALUE);

                if (nitems == selectors[selector].nitems)
                    break;

                if (array[i] > (1UL << selectors[selector].nbits) - 1)
                    break;

                data |= (array[i] << shift);

                shift += selectors[selector].nbits;

                nitems++;
            }

            if (nitems == selectors[selector].nitems || index + nitems == n) {
                Fwrite(&data, sizeof data, 1, fp);

                nbytes += sizeof data;

                index += nitems;

                break;
            }

        } /* End for selector ... */

    } /* End while index < n */

    return nbytes;
}

size_t simple9_decode(uint32_t **array, size_t *n, FILE *fp)
{
    uint32_t data;
    uint32_t select;
    uint32_t mask;

    size_t nbytes;
    size_t nitems;
    size_t i;

    assert(array);
    assert(n);
    assert(fp);

    nbytes = vbyte_decode(n, fp);

    /* Look up at the sky. So many stars. It's... beautiful. */
    *array = malloc(*n * sizeof **array);
    assert(*array);

    nitems = 0;

    while (nitems < *n) {
        Fread(&data, sizeof data, 1, fp);

        nbytes += sizeof data;

        select = data & SELECTOR_MASK;

        data >>= SELECTOR_BITS;

        mask = (1 << selectors[select].nbits) - 1;

        for (i = 0; i < selectors[select].nitems; i++) {
            (*array)[nitems] = data & mask;

            nitems++;

            if (nitems == *n)
                break;

            data >>= selectors[select].nbits;
        }
    }

    return nbytes;
}

size_t simple9_decode_unrolled(uint32_t **array, size_t *n, FILE *fp)
{
    uint32_t data;
    uint32_t select;
    uint32_t *ptr;

    size_t nbytes;
    size_t nitems;

    assert(array);
    assert(n);
    assert(fp);

    nbytes = vbyte_decode(n, fp);

    /* Due to the unrolled decoding loop there is no bounds checking.
     * To prevent a segmentation or bus fault during decoding we need
     * to allocated some extra space. The maximum offset the decoder
     * can run out of bounds is (the maximum elements that can be
     * packed into a word) - 1. */
    *array = malloc((*n + selectors[0].nitems - 1) * sizeof **array);
    assert(*array);

    ptr = *array;

    nitems = 0;

    while (nitems < *n) {
        Fread(&data, sizeof data, 1, fp);

        nbytes += sizeof data;

        select = data & SELECTOR_MASK;

        data >>= SELECTOR_BITS;

        switch (select) {
        case 0: /* 28 -- 1 bit elements */
            ptr[nitems++] = (data) & 1;
            ptr[nitems++] = (data >> 1) & 1;
            ptr[nitems++] = (data >> 2) & 1;
            ptr[nitems++] = (data >> 3) & 1;
            ptr[nitems++] = (data >> 4) & 1;
            ptr[nitems++] = (data >> 5) & 1;
            ptr[nitems++] = (data >> 6) & 1;
            ptr[nitems++] = (data >> 7) & 1;
            ptr[nitems++] = (data >> 8) & 1;
            ptr[nitems++] = (data >> 9) & 1;
            ptr[nitems++] = (data >> 10) & 1;
            ptr[nitems++] = (data >> 11) & 1;
            ptr[nitems++] = (data >> 12) & 1;
            ptr[nitems++] = (data >> 13) & 1;
            ptr[nitems++] = (data >> 14) & 1;
            ptr[nitems++] = (data >> 15) & 1;
            ptr[nitems++] = (data >> 16) & 1;
            ptr[nitems++] = (data >> 17) & 1;
            ptr[nitems++] = (data >> 18) & 1;
            ptr[nitems++] = (data >> 19) & 1;
            ptr[nitems++] = (data >> 20) & 1;
            ptr[nitems++] = (data >> 21) & 1;
            ptr[nitems++] = (data >> 22) & 1;
            ptr[nitems++] = (data >> 23) & 1;
            ptr[nitems++] = (data >> 24) & 1;
            ptr[nitems++] = (data >> 25) & 1;
            ptr[nitems++] = (data >> 26) & 1;
            ptr[nitems++] = (data >> 27) & 1;
            break;

        case 1: /* 14 -- 2 bit elements */
            ptr[nitems++] = (data) & 3;
            ptr[nitems++] = (data >> 2) & 3;
            ptr[nitems++] = (data >> 4) & 3;
            ptr[nitems++] = (data >> 6) & 3;
            ptr[nitems++] = (data >> 8) & 3;
            ptr[nitems++] = (data >> 10) & 3;
            ptr[nitems++] = (data >> 12) & 3;
            ptr[nitems++] = (data >> 14) & 3;
            ptr[nitems++] = (data >> 16) & 3;
            ptr[nitems++] = (data >> 18) & 3;
            ptr[nitems++] = (data >> 20) & 3;
            ptr[nitems++] = (data >> 22) & 3;
            ptr[nitems++] = (data >> 24) & 3;
            ptr[nitems++] = (data >> 26) & 3;
            break;

        case 2: /* 9 -- 3 bit elements (1 wasted bit) */
            ptr[nitems++] = (data) & 7;
            ptr[nitems++] = (data >> 3) & 7;
            ptr[nitems++] = (data >> 6) & 7;
            ptr[nitems++] = (data >> 9) & 7;
            ptr[nitems++] = (data >> 12) & 7;
            ptr[nitems++] = (data >> 15) & 7;
            ptr[nitems++] = (data >> 18) & 7;
            ptr[nitems++] = (data >> 21) & 7;
            ptr[nitems++] = (data >> 24) & 7;
            break;

        case 3: /* 7 -- 4 bit elements */
            ptr[nitems++] = (data) & 15;
            ptr[nitems++] = (data >> 4) & 15;
            ptr[nitems++] = (data >> 8) & 15;
            ptr[nitems++] = (data >> 12) & 15;
            ptr[nitems++] = (data >> 16) & 15;
            ptr[nitems++] = (data >> 20) & 15;
            ptr[nitems++] = (data >> 24) & 15;
            break;

        case 4: /* 5 -- 5 bit elements (3 wasted bits) */
            ptr[nitems++] = (data) & 31;
            ptr[nitems++] = (data >> 5) & 31;
            ptr[nitems++] = (data >> 10) & 31;
            ptr[nitems++] = (data >> 15) & 31;
            ptr[nitems++] = (data >> 20) & 31;
            break;

        case 5: /* 4 -- 7 bit elements */
            ptr[nitems++] = (data) & 127;
            ptr[nitems++] = (data >> 7) & 127;
            ptr[nitems++] = (data >> 14) & 127;
            ptr[nitems++] = (data >> 21) & 127;
            break;

        case 6: /* 3 -- 9 bit elements (1 wasted bit) */
            ptr[nitems++] = (data) & 511;
            ptr[nitems++] = (data >> 9) & 511;
            ptr[nitems++] = (data >> 18) & 511;
            break;

        case 7: /* 2 -- 14 bit elements */
            ptr[nitems++] = (data) & 16383;
            ptr[nitems++] = (data >> 14) & 16383;
            break;

        case 8: /* 1 -- 28 bit element */
            ptr[nitems++] = data;
            break;
        }
    }

    return nbytes;
}
