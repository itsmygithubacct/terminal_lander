#ifndef PCM_WAV_H
#define PCM_WAV_H

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t pcm_wav_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t pcm_wav_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Load the exact runtime contract used by the in-process game mixer. */
static bool pcm_wav_load_mono_44100(const char *path, int16_t **out_data,
                                    int *out_frames)
{
    uint8_t header[12], chunk[8], format[16];
    long data_offset = -1;
    uint32_t data_bytes = 0;
    bool format_ok = false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;

    if (fread(header, 1, sizeof header, file) != sizeof header ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        goto fail;

    while (fread(chunk, 1, sizeof chunk, file) == sizeof chunk) {
        uint32_t size = pcm_wav_u32le(chunk + 4);
        long payload = ftell(file);
        if (payload < 0) goto fail;
        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (size < sizeof format ||
                fread(format, 1, sizeof format, file) != sizeof format)
                goto fail;
            format_ok = pcm_wav_u16le(format) == 1 &&
                        pcm_wav_u16le(format + 2) == 1 &&
                        pcm_wav_u32le(format + 4) == 44100 &&
                        pcm_wav_u16le(format + 14) == 16;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_offset = payload;
            data_bytes = size;
        }
        if (fseek(file, payload + (long)size + (long)(size & 1u), SEEK_SET) != 0)
            goto fail;
    }

    if (!format_ok || data_offset < 0 || data_bytes == 0 ||
        (data_bytes & 1u) != 0 || data_bytes > 128u * 1024u * 1024u ||
        data_bytes / 2u > INT_MAX)
        goto fail;
    if (fseek(file, data_offset, SEEK_SET) != 0) goto fail;

    uint8_t *encoded = malloc(data_bytes);
    int16_t *samples = malloc((size_t)(data_bytes / 2u) * sizeof *samples);
    if (!encoded || !samples) {
        free(encoded);
        free(samples);
        goto fail;
    }
    if (fread(encoded, 1, data_bytes, file) != data_bytes) {
        free(encoded);
        free(samples);
        goto fail;
    }
    for (uint32_t i = 0; i < data_bytes / 2u; ++i)
        samples[i] = (int16_t)pcm_wav_u16le(encoded + i * 2u);
    free(encoded);
    fclose(file);
    *out_data = samples;
    *out_frames = (int)(data_bytes / 2u);
    return true;

fail:
    fclose(file);
    return false;
}

#endif
