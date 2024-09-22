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
#define MPEG1_TEXTURE_WIDTH 512
#define MPEG1_TEXTURE_HEIGHT 256
#define BUFFER_SIZE (2*1024*1024)   // 2MB buffer for streaming
#define MIN_BUFFER_FILL (BUFFER_SIZE / 4)  // Minimum buffer fill before playback

static plm_t *plm;
static pvr_ptr_t texture;
static int width, height;
static double target_frame_time;

float audio_time;
float audio_interval;
snd_stream_hnd_t snd_hnd;
__attribute__((aligned(32))) unsigned int snd_buf[0x10000 / 4];
static int snd_mod_start = 0;
static int snd_mod_size = 0;

static uint8_t *stream_buffer = NULL;
static plm_buffer_t *plm_buffer = NULL;

void initialize_stream_buffer() {
    if (stream_buffer) {
        free(stream_buffer);
    }
    stream_buffer = (uint8_t *)malloc(BUFFER_SIZE);
    if (!stream_buffer) {
        DEBUG_PRINT("Failed to allocate stream buffer");
        exit(1);
    }
    plm_buffer = plm_buffer_create_with_capacity(BUFFER_SIZE);
    if (!plm_buffer) {
        DEBUG_PRINT("Failed to create PLM buffer");
        free(stream_buffer);
        exit(1);
    }
    DEBUG_PRINT("Stream buffer initialized with size: %d bytes", BUFFER_SIZE);
}

void cleanup_stream_buffer() {
    if (stream_buffer) {
        free(stream_buffer);
        stream_buffer = NULL;
    }
    if (plm_buffer) {
        plm_buffer_destroy(plm_buffer);
        plm_buffer = NULL;
    }
}

void display_draw(void)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t *vert;
    pvr_dr_state_t dr_state;
    float u = (float)width / (float)MPEG1_TEXTURE_WIDTH;
    float v = (float)height / (float)MPEG1_TEXTURE_HEIGHT;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED, MPEG1_TEXTURE_WIDTH, MPEG1_TEXTURE_HEIGHT, texture, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&hdr, &cxt);
    
    pvr_prim(&hdr, sizeof(hdr));
    
     pvr_dr_init(&dr_state);

    // Vertex 1
    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = 1;
    vert->y = 1;
    vert->z = 1;
    vert->u = 0.0f;
    vert->v = 0.0f;
    pvr_dr_commit(vert);

    // Vertex 2
    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = 640;
    vert->y = 1;
    vert->z = 1;
    vert->u = u;
    vert->v = 0.0f;
    pvr_dr_commit(vert);

    // Vertex 3
    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX;
    vert->x = 1;
    vert->y = 480;
    vert->z = 1;
    vert->u = 0.0f;
    vert->v = v;
    pvr_dr_commit(vert);

    // Vertex 4
    vert = pvr_dr_target(dr_state);
    vert->argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert->oargb = 0;
    vert->flags = PVR_CMD_VERTEX_EOL;
    vert->x = 640;
    vert->y = 480;
    vert->z = 1;
    vert->u = u;
    vert->v = v;
    pvr_dr_commit(vert);
    pvr_dr_finish();
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
    // DEBUG_PRINT("YUV Averages: Y=%.2f, U=%.2f, V=%.2f", y_avg, u_avg, v_avg);
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

    w = frame->width >> 4;
    h = frame->height >> 4;
    stride_value = (w >> 1); /* 16 pixel / 2 */

    *stride_reg = (*stride_reg & 0xffffffe0) | (stride_value & 0x01f);

    *d = ((unsigned int)dest) & 0xffffff;
    *cfg = 0x00000f1f;

    verify_yuv_data(src, frame->width, frame->height);

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
}

void *sound_callback(snd_stream_hnd_t hnd, int size, int *size_out)
{
    plm_samples_t *sample;
    unsigned int *dest = snd_buf;
    unsigned int *src;
    int out = 0;

    src = snd_buf + snd_mod_start / 4;
    for (int i = 0; i < snd_mod_size / 4; i++)
        *dest++ = *src++;
    out += snd_mod_size;

    while (size > out)
    {
        sample = plm_decode_audio(plm);
        if (sample == NULL)
        {
            audio_time += audio_interval;
            break;
        }
        audio_time = sample->time;

        src = (unsigned int *)sample->pcm;
        for (int i = 0; i < 1152 / 2; i++)
            *dest++ = *src++;
        out += 1152 * 2;
    }

    snd_mod_start = size;
    snd_mod_size = out - size;
    *size_out = size;

    return (void *)snd_buf;
}

int Mpeg1Play(const char *filename, unsigned int buttons)
{
    int cancel = 0;
    file_t file;

    initialize_stream_buffer();
 
    file = fs_open(filename, O_RDONLY);
    if (file < 0) {
        DEBUG_PRINT("Failed to open file: %s", filename);
        cleanup_stream_buffer();
        return -1;
    }

    // Initial buffer fill
    int total_bytes_read = 0;
    while (total_bytes_read < MIN_BUFFER_FILL) {
        int bytes_read = fs_read(file, stream_buffer + total_bytes_read, BUFFER_SIZE - total_bytes_read);
        if (bytes_read <= 0) break;
        total_bytes_read += bytes_read;
    }
    plm_buffer_write(plm_buffer, stream_buffer, total_bytes_read);
    DEBUG_PRINT("Initial buffer fill: %d bytes", total_bytes_read);

    plm = plm_create_with_buffer(plm_buffer, 1);
    if (!plm) {
        DEBUG_PRINT("Failed to create PLM");
        fs_close(file);
        cleanup_stream_buffer();
        return -1;
    }

    texture = pvr_mem_malloc(MPEG1_TEXTURE_WIDTH * MPEG1_TEXTURE_HEIGHT * 2);
    if (!texture) {
        DEBUG_PRINT("Failed to allocate texture memory");
        plm_destroy(plm);
        fs_close(file);
        cleanup_stream_buffer();
        return -1;
    }

    width = plm_get_width(plm);
    height = plm_get_height(plm);
    double framerate = plm_get_framerate(plm);
    target_frame_time = 1000.0 / framerate;

    DEBUG_PRINT("Video info: %dx%d @ %.2f fps", width, height, framerate);
    DEBUG_PRINT("Target frame time: %.2f ms", target_frame_time);

    PVR_SET(PVR_YUV_ADDR, (((unsigned int)texture) & 0xffffff));
    PVR_SET(PVR_YUV_CFG, (((MPEG1_TEXTURE_HEIGHT / 16) - 1) << 8) | ((MPEG1_TEXTURE_WIDTH / 16) - 1));
    PVR_GET(PVR_YUV_CFG);

    plm_frame_t *frame = plm_decode_video(plm);
    int decoded = 1;

    int samplerate = plm_get_samplerate(plm);
    DEBUG_PRINT("Audio samplerate: %d", samplerate);
    snd_mod_size = 0;
    snd_mod_start = 0;
    audio_time = 0.0f;
    snd_hnd = snd_stream_alloc(sound_callback, 0x10000);
    snd_stream_volume(snd_hnd, 0xff);
    snd_stream_queue_enable(snd_hnd);
    snd_stream_start(snd_hnd, samplerate, 0);
    snd_stream_queue_go(snd_hnd);
    audio_interval = audio_time;

    uint64_t last_time = timer_ms_gettime64(), current_time;
    uint32_t frame_count = 0;
    uint32_t buffer_refill_count = 0;
  
    while (!cancel)
    {
        frame_count++;
        current_time = timer_ms_gettime64();
        
        if (current_time - last_time >= 1000) {
              DEBUG_PRINT("FPS: %lu, Buffer refills: %lu, Buffer remaining: %d", 
                        (unsigned long)frame_count, 
                        (unsigned long)buffer_refill_count, 
                        plm_buffer_get_remaining(plm_buffer));
            frame_count = 0;
            buffer_refill_count = 0;
            last_time = current_time;
        }
 
        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
        if (buttons && ((st->buttons & buttons) == buttons))
            cancel = 1;
        if (st->buttons == 0x60e)
            cancel = 2;
        MAPLE_FOREACH_END()

        if ((audio_time - audio_interval) >= frame->time)
        {
            frame = plm_decode_video(plm);
            if (!frame)
                break;
            decoded = 1;
        }
    
        pvr_wait_ready();
        snd_stream_poll(snd_hnd);
        pvr_scene_begin();

        pvr_list_begin(PVR_LIST_OP_POLY);
        if (decoded)
        {
            app_on_video(plm, frame, 0);
            decoded = 0;
        }
        display_draw();
        pvr_list_finish();

        pvr_scene_finish();

        if (plm_buffer_get_remaining(plm_buffer) < MIN_BUFFER_FILL) {
            int bytes_to_read = BUFFER_SIZE - plm_buffer_get_remaining(plm_buffer);
            int bytes_read = fs_read(file, stream_buffer, bytes_to_read);
            if (bytes_read > 0) {
                    plm_buffer_write(plm_buffer, stream_buffer, bytes_read);
                buffer_refill_count++;
                DEBUG_PRINT("Buffer refilled: %d bytes, Total remaining: %d", 
                            bytes_read, plm_buffer_get_remaining(plm_buffer));
            }
        }
    }

    plm_destroy(plm);
    pvr_mem_free(texture);
    snd_stream_destroy(snd_hnd);
    fs_close(file);
    cleanup_stream_buffer();

    return cancel;
}

