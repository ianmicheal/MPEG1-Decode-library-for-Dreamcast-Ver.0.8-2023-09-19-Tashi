#include <kos.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "mpeg.h"

static plm_t *plm;

/* Textures */
static pvr_poly_hdr_t hdr;
static pvr_vertex_t vert[4];
static pvr_ptr_t texture;

static int width, height;
/* Output texture width and height initial values
   You can choose from 32, 64, 128, 256, 512, 1024 */
#define MPEG_TEXTURE_WIDTH 512
#define MPEG_TEXTURE_HEIGHT 256

float audio_time;
float audio_interval;
snd_stream_hnd_t snd_hnd;

static int snd_mod_start = 0;
static int snd_mod_size = 0;

#define SOUND_BUFFER (16 * 1024)
//65536
uint32_t *snd_buf;

static void *sound_callback(snd_stream_hnd_t hnd, int size, int *size_out) {
    plm_samples_t *sample;
    uint32_t *dest = snd_buf;
    uint32_t *src;
    int i, out = 0;

    src = snd_buf + snd_mod_start / 4;
    for(i = 0; i < snd_mod_size / 4; i++)
        *dest++ = *src++;
    out += snd_mod_size;

    while(size > out) {
        sample = plm_decode_audio(plm);
        if(sample == NULL) {
            audio_time += audio_interval;
            break;
        }
        audio_time = sample->time;

        src = (uint32_t *)sample->pcm;
        for(int i = 0; i < 1152 / 2; i++)
            *dest++ = *src++;
        out += 1152 * 2;
    }

    snd_mod_start = size;
    snd_mod_size = out - size;
    *size_out = size;

    return (void *)snd_buf;
}

static int setup_graphics(void) {
    pvr_poly_cxt_t cxt;
    float u, v;

    if(!(texture = pvr_mem_malloc(MPEG_TEXTURE_WIDTH * MPEG_TEXTURE_HEIGHT * 2))) {
        fprintf(stderr, "Failed to allocate PVR memory!\n");
        return -1;
    }
    
    /* Set SQ to YUV converter. */
    PVR_SET(PVR_YUV_ADDR, (((uint32_t)texture) & 0xffffff));
    /* Divide texture width and texture height by 16 and subtract 1.
       The actual values to set are 1, 3, 7, 15, 31, 63. */
    PVR_SET(PVR_YUV_CFG, (((MPEG_TEXTURE_HEIGHT / 16) - 1) << 8) | 
                         ((MPEG_TEXTURE_WIDTH / 16) - 1));
    PVR_GET(PVR_YUV_CFG);

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, 
                    PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED, 
                    MPEG_TEXTURE_WIDTH, MPEG_TEXTURE_HEIGHT, 
                    texture, 
                    PVR_FILTER_BILINEAR);
    pvr_poly_compile(&hdr, &cxt);

    hdr.mode3 |= PVR_TXRFMT_STRIDE;

    u = (float)width / MPEG_TEXTURE_WIDTH;
    v = (float)height / MPEG_TEXTURE_HEIGHT;

    vert[0].z     = vert[1].z     = vert[2].z     = vert[3].z     = 1.0f; 
    vert[0].argb  = vert[1].argb  = vert[2].argb  = vert[3].argb  = 
        PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);    
    vert[0].oargb = vert[1].oargb = vert[2].oargb = vert[3].oargb = 0;  
    vert[0].flags = vert[1].flags = vert[2].flags = PVR_CMD_VERTEX;         
    vert[3].flags = PVR_CMD_VERTEX_EOL;

    vert[0].x = 0.0f;
    vert[0].y = 0.0f;
    vert[0].u = 0.0f;
    vert[0].v = 0.0f;

    vert[1].x = 640.0f;
    vert[1].y = 0.0f;
    vert[1].u = u;
    vert[1].v = 0.0f;

    vert[2].x = 0.0f;
    vert[2].y = 480.0f;
    vert[2].u = 0.0f;
    vert[2].v = v;

    vert[3].x = 640.0f;
    vert[3].y = 480.0f;
    vert[3].u = u;
    vert[3].v = v;

    return 0;
}

static void upload_frame(plm_frame_t *frame) {
    uint32_t *src;
    int x, y, w, h, i;

    if(!frame)
        return;

    src = frame->display;

    /* Set frame size. */
    w = frame->width >> 4;
    h = frame->height >> 4;

    for(y = 0; y < h; y++){
        for(x = 0; x < w; x++, src += 96) {
            sq_cpy((void *)PVR_TA_YUV_CONV, (void *)src, 384);
        }

        /* Send dummy mb */
        for(i = 0; i < 32 - w; i++) {
            sq_set((void *)PVR_TA_YUV_CONV, 0, 384);
        }
    }

    for(i = 0; i < 16 - h; i++) {
        sq_set((void *)PVR_TA_YUV_CONV, 0, 384 * 32);
    }
}

static void draw_frame(void) {
    pvr_prim(&hdr, sizeof(pvr_poly_hdr_t));
    pvr_prim(&vert[0], sizeof(pvr_vertex_t));
    pvr_prim(&vert[1], sizeof(pvr_vertex_t));
    pvr_prim(&vert[2], sizeof(pvr_vertex_t));
    pvr_prim(&vert[3], sizeof(pvr_vertex_t));
}

int mpeg_play(const char *filename, uint32_t buttons) {
    int cancel = 0;
    plm_frame_t *frame;
    int decoded;
    int samplerate;

    plm = plm_create_with_filename(filename);
    if(!plm)
        return -1;

    snd_buf = memalign(32, SOUND_BUFFER);
    if(snd_buf) {
        printf("Shit happens\n");
    }
    
    width = plm_get_width(plm);
    height = plm_get_height(plm);

    if(setup_graphics() < 0)
        return -1;

    /* First frame */
    frame = plm_decode_video(plm);
    decoded = 1;

    /* Init sound stream. */
    samplerate = plm_get_samplerate(plm);
    snd_mod_size = 0;
    snd_mod_start = 0;
    audio_time = 0.0f;
    snd_hnd = snd_stream_alloc(sound_callback, 0x10000);
    snd_stream_volume(snd_hnd, 0xff);
    snd_stream_start(snd_hnd, samplerate, 0);
    audio_interval = audio_time;

    while(!cancel) {
        /* Check cancel buttons. */
        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
        if(buttons && ((st->buttons & buttons) == buttons))
            cancel = 1; /* Push cancel buttons */
        if(st->buttons == 0x60e)
            cancel = 2; /* ABXY + START (Software reset) */
        MAPLE_FOREACH_END()

        /* Decode */
        if((audio_time - audio_interval) >= frame->time) {
            frame = plm_decode_video(plm);
            if(!frame)
                break;
            decoded = 1;
        }
        snd_stream_poll(snd_hnd);

        /* Render */
        pvr_wait_ready();
        pvr_scene_begin();
        if(decoded) {
            upload_frame(frame);
            decoded = 0;
        }
        pvr_list_begin(PVR_LIST_OP_POLY);
        draw_frame();
        pvr_list_finish();
        pvr_scene_finish();
    }

    free(snd_buf);
    plm_destroy(plm);
    pvr_mem_free(texture);
    snd_stream_destroy(snd_hnd);

    return cancel;
}
