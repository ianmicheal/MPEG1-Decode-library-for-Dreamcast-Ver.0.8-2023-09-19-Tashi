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

snd_stream_hnd_t snd_hnd;
__attribute__((aligned(32))) unsigned int snd_buf[0x10000 / 4];

void display_draw(void)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;
    float u = (float)width / (float)MPEG1_TEXTURE_WIDTH;
    float v = (float)height / (float)MPEG1_TEXTURE_HEIGHT;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED, MPEG1_TEXTURE_WIDTH, MPEG1_TEXTURE_HEIGHT, texture, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&hdr, &cxt);

    // hdr.mode3 |= 0x02000000; /* stride */
    pvr_prim(&hdr, sizeof(hdr));

    vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert.oargb = 0;
    vert.flags = PVR_CMD_VERTEX;

    vert.x = 1;
    vert.y = 1;
    vert.z = 1;
    vert.u = 0.0f;
    vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.x = 640;
    vert.y = 1;
    vert.z = 1;
    vert.u = u;
    vert.v = 0.0;
    pvr_prim(&vert, sizeof(vert));

    vert.x = 1;
    vert.y = 480;
    vert.z = 1;
    vert.u = 0.0f;
    vert.v = v;
    pvr_prim(&vert, sizeof(vert));

    vert.x = 640;
    vert.y = 480;
    vert.z = 1;
    vert.u = u;
    vert.v = v;
    vert.flags = PVR_CMD_VERTEX_EOL;
    pvr_prim(&vert, sizeof(vert));
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

// Define a buffer for audio samples
#define AUDIO_SAMPLE_BUFFER_SIZE (1152 * 2 * 10) // Adjust the size as needed

// Static variables for sound callback
static int mod_start = 0;
static int mod_size = 0;
static unsigned int audio_sample_buffer[AUDIO_SAMPLE_BUFFER_SIZE];
 //Ian micheal added call back sound reset and buffer
// Function to reset sound callback's static variables
void reset_sound_callback()
{
    mod_start = 0;
    mod_size = 0;
}

// Sound callback function
void *sound_callback(snd_stream_hnd_t hnd, int size, int *size_out)
{
    unsigned int *dest = audio_sample_buffer;
    unsigned int *src;
    int out = 0;

    src = audio_sample_buffer + mod_start / 4;
    for (int i = 0; i < mod_size / 4; i++)
        *dest++ = *src++;
    out += mod_size;

    while (size > out)
    {
        plm_samples_t *sample = plm_decode_audio(plm);

        if (sample == NULL)
        {
            reset_sound_callback(); // Reset static variables when audio ends
            break;
        }

        src = (unsigned int *)sample->pcm;
        for (int i = 0; i < 1152 / 2; i++)
            *dest++ = *src++;
        out += 1152 * 2;
    }

    mod_start = size;
    mod_size = out - size;
    *size_out = size;

    return (void *)audio_sample_buffer;
}


int Mpeg1Play(const char *filename, unsigned int buttons)
{
    int cancel = 0;

    plm = plm_create_with_filename(filename);
    if (!plm)
        return -1;
    texture = pvr_mem_malloc(512 * 256 * 2);
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
    float start_time = (float)timer_ms_gettime64() / 1000.0;
    float playing_time = 0.0f;
    float frame_time = 1.0f / (float)plm_get_framerate(plm);
    int decoded = 1;
    //Ian micheal added call back sound reset and buffer
    reset_sound_callback();
    /* Init sound stream. */
    int samplerate = plm_get_samplerate(plm);
    snd_hnd = snd_stream_alloc(sound_callback, 0x10000);
    snd_stream_volume(snd_hnd, 0xff);
    snd_stream_queue_enable(snd_hnd);
    snd_stream_start(snd_hnd, samplerate, 0);
    snd_stream_queue_go(snd_hnd);

    while (!cancel)
    {
        /* Check cancel buttons. */
        if (buttons)
        {
            MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
            if ((st->buttons & buttons) == buttons)
                cancel = 1;
            MAPLE_FOREACH_END()
        }

        /* Decode */
        playing_time = ((float)timer_ms_gettime64() / 1000.0) - start_time;
        if ((frame->time - playing_time) < frame_time)
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
     //Ian micheal added call back sound reset and buffer
    reset_sound_callback();
    plm_destroy(plm);
    pvr_mem_free(texture);
    snd_stream_destroy(snd_hnd);

    return 0;
}
