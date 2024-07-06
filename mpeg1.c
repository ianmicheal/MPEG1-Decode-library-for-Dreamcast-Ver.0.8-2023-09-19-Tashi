#include <kos.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "mpeg1.h"

static plm_t *plm;

/* textures */
static pvr_ptr_t texture;
static int width, height;
// Output texture width and height initial values
// You can choose from 32, 64, 128, 256, 512, 1024
#define MPEG1_TEXTURE_WIDTH 512
#define MPEG1_TEXTURE_HEIGHT 256

float audio_time;
float audio_interval;
snd_stream_hnd_t snd_hnd;
__attribute__((aligned(32))) unsigned int snd_buf[0x10000 / 4];
static int snd_mod_start = 0;
static int snd_mod_size = 0;
//IAN MICHEAL 7/6/2024 updated to use much faster Direct render state 
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
    // hdr.mode3 |= 0x02000000; /* stride */
    
    pvr_prim(&hdr, sizeof(hdr));
    
    pvr_dr_init(dr_state);

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
}

void app_on_video(plm_t *mpeg, plm_frame_t *frame, void *user)
{
    unsigned int *dest = (unsigned int *)texture;
    unsigned int *src = (unsigned int *)frame->display;

    volatile unsigned int *d = (volatile unsigned int *)0xa05f8148;
    volatile unsigned int *cfg = (volatile unsigned int *)0xa05f814c;

    volatile unsigned int *stride_reg = (volatile unsigned int *)0xa05f80e4;
    int stride_value;
    int stride = 0;

    int x, y, w, h, i;

    if (!frame)
        return;

    /* set frame size. */
    w = frame->width >> 4;
    h = frame->height >> 4;
    stride_value = (w >> 1); /* 16 pixel / 2 */

    /* Set Stride value. */
    *stride_reg &= 0xffffffe0;
    *stride_reg |= stride_value & 0x01f;

    /* Set SQ to YUV converter. */
    *d = ((unsigned int)dest) & 0xffffff;
    *cfg = 0x00000f1f;
    x = *cfg; /* read on once */

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++, src += 96)
        {
            sq_cpy((void *)0x10800000, (void *)src, 384);
        }
        if (!stride)
        {
            /* Send dummy mb */
            for (i = 0; i < 32 - w; i++)
            {
                sq_set((void *)0x10800000, 0, 384);
            }
        }
    }
    for (i = 0; i < 16 - h; i++)
    {
        if (!stride)
            sq_set((void *)0x10800000, 0, 384 * 32);
        else
            sq_set((void *)0x10800000, 0, 384 * w);
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

    plm = plm_create_with_filename(filename);
    if (!plm)
        return -1;
    texture = pvr_mem_malloc(MPEG1_TEXTURE_WIDTH * MPEG1_TEXTURE_HEIGHT * 2);
    width = plm_get_width(plm);
    height = plm_get_height(plm);

    /* Set SQ to YUV converter. */
    PVR_SET(PVR_YUV_ADDR, (((unsigned int)texture) & 0xffffff));
    // Divide texture width and texture height by 16 and subtract 1.
    // The actual values to set are 1, 3, 7, 15, 31, 63.
    PVR_SET(PVR_YUV_CFG_1, (((MPEG1_TEXTURE_HEIGHT / 16) - 1) << 8) | ((MPEG1_TEXTURE_WIDTH / 16) - 1));
    PVR_GET(PVR_YUV_CFG_1);

    /* First frame */
    plm_frame_t *frame = plm_decode_video(plm);
    int decoded = 1;

    /* Init sound stream. */
    int samplerate = plm_get_samplerate(plm);
    snd_mod_size = 0;
    snd_mod_start = 0;
    audio_time = 0.0f;
    snd_hnd = snd_stream_alloc(sound_callback, 0x10000);
    snd_stream_volume(snd_hnd, 0xff);
    snd_stream_queue_enable(snd_hnd);
    snd_stream_start(snd_hnd, samplerate, 0);
    snd_stream_queue_go(snd_hnd);
    audio_interval = audio_time;

    while (!cancel)
    {
        /* Check cancel buttons. */
        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
        if (buttons && ((st->buttons & buttons) == buttons))
            cancel = 1; /* Push cancel buttons */
        if (st->buttons == 0x60e)
            cancel = 2; /* ABXY + START (Software reset) */
        MAPLE_FOREACH_END()

        /* Decode */
        if ((audio_time - audio_interval) >= frame->time)
        {
            frame = plm_decode_video(plm);
            if (!frame)
                break;
            decoded = 1;
        }
        snd_stream_poll(snd_hnd);

        /* Render */
        pvr_wait_ready();
        pvr_scene_begin();
        if (decoded)
        {
            app_on_video(plm, frame, 0);
            decoded = 0;
        }
        pvr_list_begin(PVR_LIST_OP_POLY);
        display_draw();
        pvr_list_finish();
        pvr_scene_finish();
    }

    plm_destroy(plm);
    pvr_mem_free(texture);
    snd_stream_destroy(snd_hnd);

    return cancel;
}
