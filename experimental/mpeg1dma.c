/*
 * PL_MPEG - MPEG1 Video decoder, MP2 Audio decoder, MPEG-PS demuxer
 * -------------------------------------------------------------
 *
 * Original Author: Dominic Szablewski - https://phoboslab.org
 * Dreamcast Port: Ian Michael (2023/2024)
 * Dreamcast Port:Twada SH4 Optimizing and sound [making it use-able at all]
 * Further optimizing functions for Dreamcast
 * SH4 inline assembly by Ian Michael
 *
 * LICENSE: The MIT License (MIT)
 * ------------------------------
 * Copyright (c) 2019 Dominic Szablewski
 * Copyright (c) 2024 Ian Michael
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */




#include <kos.h>
#include <dc/pvr.h>
#include <dc/sound/stream.h>
#include <arch/cache.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "mpeg1.h"

#define DEBUG_PRINT(fmt, ...) printf("DEBUG [%s:%d]: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define TEXTURE_WIDTH 512
#define TEXTURE_HEIGHT 256


static plm_t *plm;
static pvr_ptr_t texture;
static int width, height;
static snd_stream_hnd_t snd_hnd;
static __attribute__((aligned(32))) unsigned int snd_buf[0x10000 / 4];
static int snd_mod_start = 0;
static int snd_mod_size = 0;
static int audio_ended = 0;

uint8 __attribute__((aligned(32))) op_buf[VERTBUF_SIZE];
uint8 __attribute__((aligned(32))) tr_buf[VERTBUF_SIZE];

void display_draw(pvr_poly_hdr_t *hdr)
{
    if (!hdr) {
        DEBUG_PRINT("Error: Null header in display_draw");
        return;
    }

    pvr_vertex_t __attribute__((aligned(32))) verts[4];
    float u = (float)width / (float)TEXTURE_WIDTH;
    float v = (float)height / (float)TEXTURE_HEIGHT;

    for (int i = 0; i < 4; i++) {
        verts[i].flags = PVR_CMD_VERTEX;
        verts[i].argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
        verts[i].oargb = 0;
        verts[i].z = 1.0f;
    }
    verts[3].flags = PVR_CMD_VERTEX_EOL;

    verts[0].x = 0;   verts[0].y = 0;   verts[0].u = 0.0f; verts[0].v = 0.0f;
    verts[1].x = 640; verts[1].y = 0;   verts[1].u = u;    verts[1].v = 0.0f;
    verts[2].x = 0;   verts[2].y = 480; verts[2].u = 0.0f; verts[2].v = v;
    verts[3].x = 640; verts[3].y = 480; verts[3].u = u;    verts[3].v = v;

    pvr_list_prim(PVR_LIST_OP_POLY, hdr, sizeof(pvr_poly_hdr_t));
    pvr_list_prim(PVR_LIST_OP_POLY, verts, sizeof(verts));

    DEBUG_PRINT("Display draw: u=%.4f, v=%.4f", u, v);
}

void verify_yuv_data(unsigned int *src, int w, int h)
{
    int y_sum = 0, u_sum = 0, v_sum = 0;
    int count = 0;
    for (int i = 0; i < w * h * 2 / 4; i++) {  // YUV422 format
        unsigned int pixel = src[i];
        y_sum += (pixel & 0xFF) + ((pixel >> 16) & 0xFF);
        u_sum += (pixel >> 8) & 0xFF;
        v_sum += (pixel >> 24) & 0xFF;
        count += 2;
    }
    float y_avg = (float)y_sum / count;
    float u_avg = (float)u_sum / (count / 2);
    float v_avg = (float)v_sum / (count / 2);
    DEBUG_PRINT("YUV Averages: Y=%.2f, U=%.2f, V=%.2f", y_avg, u_avg, v_avg);
}

void app_on_video(plm_t *mpeg, plm_frame_t *frame, void *user)
{
    unsigned int *dest = (unsigned int *)texture;
    unsigned int *src = (unsigned int *)frame->display;
    volatile unsigned int *d = (volatile unsigned int *)0xa05f8148;
    volatile unsigned int *cfg = (volatile unsigned int *)0xa05f814c;
    volatile unsigned int *stride_reg = (volatile unsigned int *)0xa05f80e4;
    int stride_value;
    int x, y, w, h, i;
 
    snd_stream_poll(snd_hnd);

    if (!frame)
        return;

    /* set frame size. */
    w = frame->width >> 4;
    h = frame->height >> 4;
    stride_value = (w >> 1); /* 16 pixel / 2 */
    DEBUG_PRINT("Frame dimensions: w=%d, h=%d, stride_value=%d", w, h, stride_value);

    /* Set Stride value. */
    *stride_reg = (*stride_reg & 0xffffffe0) | (stride_value & 0x01f);

    /* Set SQ to YUV converter. */
    *d = ((unsigned int)dest) & 0xffffff;
    *cfg = 0x00000f1f;

    verify_yuv_data(src, frame->width, frame->height);

    DEBUG_PRINT("Starting frame data transfer");
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++, src += 96)
        {
            sq_cpy((void *)0x10800000, (void *)src, 384);
        }
        
        /* Send dummy mb to align to 32 macroblocks */
        for (i = 0; i < 32 - w; i++)
        {
            sq_set((void *)0x10800000, 0, 384);
        }
    }

    /* Pad vertically to 16 macroblocks */
    for (i = 0; i < 16 - h; i++)
    {
        sq_set((void *)0x10800000, 0, 384 * 32);
    }



    DEBUG_PRINT("Frame data transfer complete");
}

static void *sound_callback(snd_stream_hnd_t hnd, int size, int *size_out) {
    plm_samples_t *sample;
    unsigned int *dest = (unsigned int *)snd_buf;
    unsigned int *src;
    int out = 0;

    src = (unsigned int *)snd_buf + snd_mod_start / 4;
    for (int i = 0; i < snd_mod_size / 4; i++)
        *dest++ = *src++;
    out += snd_mod_size;

    while (size > out) {
        sample = plm_decode_audio(plm);
        if (sample == NULL) {
            audio_ended = 1;
            break;
        }
        src = (unsigned int *)sample->pcm;
        for (int i = 0; i < 1152 / 2; i++)
            *dest++ = *src++;
        out += 1152 * 2;
    }

    snd_mod_start = size;
    snd_mod_size = out - size;
    *size_out = size;

    return snd_buf;
}

int Mpeg1Play(const char *filename, unsigned int buttons) {
    DEBUG_PRINT("Entering Mpeg1Play: filename=%s", filename);
    int cancel = 0;
    plm_frame_t *frame;
    pvr_poly_hdr_t hdr;
    pvr_poly_cxt_t cxt;
    uint32_t frame_count = 0;
    uint64_t last_time, current_time;

    plm = plm_create_with_filename(filename);
    if (!plm) {
        DEBUG_PRINT("Error: Failed to create plm");
        return -1;
    }

    frame = plm_decode_video(plm);
    if (!frame) {
        DEBUG_PRINT("Error: Failed to decode first video frame");
        plm_destroy(plm);
        return -1;
    }

    width = frame->width;
    height = frame->height;
    DEBUG_PRINT("Video dimensions: %dx%d", width, height);

    texture = pvr_mem_malloc(TEXTURE_WIDTH * TEXTURE_HEIGHT * 2);
    if (!texture) {
        DEBUG_PRINT("Error: Failed to allocate texture memory");
        plm_destroy(plm);
        return -1;
    }

    DEBUG_PRINT("Texture allocated at address: %p", texture);

    PVR_SET(PVR_YUV_ADDR, (((unsigned int)texture) & 0xffffff));
    PVR_SET(PVR_YUV_CFG, 0x0F);  // 16x16 macroblocks (32x16)

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED, 
                     TEXTURE_WIDTH, TEXTURE_HEIGHT, texture, PVR_FILTER_NONE);
    pvr_poly_compile(&hdr, &cxt);

    DEBUG_PRINT("PVR YUV setup complete");

    int samplerate = plm_get_samplerate(plm);
    DEBUG_PRINT("Audio samplerate: %d", samplerate);
    snd_mod_size = 0;
    snd_mod_start = 0;
    snd_hnd = snd_stream_alloc(sound_callback, 0x10000);
    if (snd_hnd < 0) {
        DEBUG_PRINT("Error: Failed to allocate sound stream");
        pvr_mem_free(texture);
        plm_destroy(plm);
        return -1;
    }
    snd_stream_volume(snd_hnd, 0xff);
    snd_stream_queue_enable(snd_hnd);
    snd_stream_start(snd_hnd, samplerate, 0);
    snd_stream_queue_go(snd_hnd);

    DEBUG_PRINT("Entering main playback loop");
    last_time = timer_ms_gettime64();
    while (!cancel && !audio_ended) {
        frame_count++;
        current_time = timer_ms_gettime64();
        
        if (current_time - last_time >= 1000) {
            DEBUG_PRINT("FPS: %u", frame_count);
            frame_count = 0;
            last_time = current_time;
        }

        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
            if (buttons && ((st->buttons & buttons) == buttons))
                cancel = 1;
        MAPLE_FOREACH_END()

        frame = plm_decode_video(plm);
        if (!frame) {
            DEBUG_PRINT("Video decoding ended");
            break;
        }

        pvr_wait_ready();
        app_on_video(plm, frame, NULL);

        pvr_scene_begin();

        pvr_set_vertbuf(PVR_LIST_OP_POLY, op_buf, VERTBUF_SIZE);
        pvr_set_vertbuf(PVR_LIST_TR_POLY, tr_buf, VERTBUF_SIZE);

        pvr_list_begin(PVR_LIST_OP_POLY);
        display_draw(&hdr);
        pvr_list_finish();
        pvr_scene_finish();

        vid_waitvbl();
        
        
    }

    DEBUG_PRINT("Playback loop ended, cleaning up");
    snd_stream_stop(snd_hnd);
    snd_stream_destroy(snd_hnd);
    plm_destroy(plm);
    pvr_mem_free(texture);
    pvr_shutdown();
    vid_shutdown();

    DEBUG_PRINT("Exiting Mpeg1Play");
    return cancel;
}