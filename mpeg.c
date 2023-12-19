#include <kos.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "mpeg.h"

struct mpeg_player_t {
    /* Decoder */
    plm_t *decoder;

    /* Textures */
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert[4];
    pvr_ptr_t texture;

    int width;
    int height;

    /* Audio */
    float audio_time;
    float audio_interval;
    snd_stream_hnd_t snd_hnd;

    int snd_mod_start;
    int snd_mod_size;

    uint32_t *snd_buf;  
};

/* Output texture width and height initial values
   You can choose from 32, 64, 128, 256, 512, 1024 */
#define MPEG_TEXTURE_WIDTH 512
#define MPEG_TEXTURE_HEIGHT 256

#define SOUND_BUFFER (16 * 1024)
//65536

static int setup_graphics(mpeg_player_t *player);
static int setup_audio(mpeg_player_t *player);

mpeg_player_t *mpeg_player_create(const char *filename) {
    mpeg_player_t *player = NULL;

    if(filename == NULL) {
        fprintf(stderr, "Filename is NULL for player init\n");
        return NULL;
    }
    
    player = malloc(sizeof(mpeg_player_t));
    if(!player) {
        fprintf(stderr, "Out of memory for player\n");
        return NULL;
    }

    player->decoder = plm_create_with_filename(filename);
    if(!player->decoder) {
        fprintf(stderr, "Out of memory for player->decoder\n");
        free(player);
        return NULL;
    }

    player->snd_buf = memalign(32, SOUND_BUFFER);
    if(!player->snd_buf) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        free(player->decoder);
        free(player);
        return NULL;
    }

    player->width = plm_get_width(player->decoder);
    player->height = plm_get_height(player->decoder);
    
    if(setup_graphics(player) < 0) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        return NULL;
    }
    
    if(setup_audio(player) < 0) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        return NULL;
    }

    //printf("Setup Success\n");

    return player;
}

mpeg_player_t *mpeg_player_create_memory(uint8_t *memory, const size_t length) {
    mpeg_player_t *player = NULL;

    if(memory == NULL) {
        fprintf(stderr, "memory is null");
        return NULL;
    }

    player = malloc(sizeof(mpeg_player_t));
    if(!player) {
        fprintf(stderr, "Out of memory for player\n");
        return NULL;
    }

    player->decoder = plm_create_with_memory(memory, length, 1);
    if(!player->decoder) {
        fprintf(stderr, "Out of memory for player->decoder\n");
        free(player);
        return NULL;
    }

    player->snd_buf = memalign(32, SOUND_BUFFER*4);
    if(!player->snd_buf) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        free(player->decoder);
        free(player);
        return NULL;
    }

    player->width = plm_get_width(player->decoder);
    player->height = plm_get_height(player->decoder);

    if(setup_graphics(player) < 0) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        return NULL;
    }
    
    if(setup_audio(player) < 0) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        return NULL;
    }

    return player;
}

void mpeg_player_destroy(mpeg_player_t *player) {
    if(player == NULL) {
        fprintf(stderr, "Player is NULL; Cannot destroy\n");
        return;
    }

    if(player->snd_buf) {
        printf("Freed sound buffer\n");
        free(player->snd_buf);
    }

    if(player->decoder) {
        printf("Freed decoder\n");
        plm_destroy(player->decoder);
    }

    if(player->texture) {
        printf("Freed texture memory\n");
        pvr_mem_free(player->texture);
    }

    snd_stream_destroy(player->snd_hnd);
}

static void *sound_callback(snd_stream_hnd_t hnd, int size, int *size_out) {
    plm_samples_t *sample;
    mpeg_player_t *player = snd_stream_get_userdata(hnd);
    uint32_t *dest = player->snd_buf;
    uint32_t *src;
    int i, out = 0;

    src = player->snd_buf + player->snd_mod_start / 4;
    for(i = 0; i < player->snd_mod_size / 4; i++)
        *dest++ = *src++;
    out += player->snd_mod_size;

    while(size > out) {
        sample = plm_decode_audio(player->decoder);
        if(sample == NULL) {
            player->audio_time += player->audio_interval;
            break;
        }
        player->audio_time = sample->time;

        src = (uint32_t *)sample->pcm;
        for(int i = 0; i < 1152 / 2; i++)
            *dest++ = *src++;
        out += 1152 * 2;
    }

    player->snd_mod_start = size;
    player->snd_mod_size = out - size;
    *size_out = size;

    return (void *)player->snd_buf;
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

static void draw_frame(mpeg_player_t *player) {
    pvr_prim(&player->hdr, sizeof(pvr_poly_hdr_t));
    pvr_prim(&player->vert[0], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[1], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[2], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[3], sizeof(pvr_vertex_t));
}

static int setup_graphics(mpeg_player_t *player) {
    pvr_poly_cxt_t cxt;
    float u, v;
    int color = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);

    if(!(player->texture = pvr_mem_malloc(MPEG_TEXTURE_WIDTH * MPEG_TEXTURE_HEIGHT * 2))) {
        fprintf(stderr, "Failed to allocate PVR memory!\n");
        return -1;
    }
    
    /* Set SQ to YUV converter. */
    PVR_SET(PVR_YUV_ADDR, (((uint32_t)player->texture) & 0xffffff));
    /* Divide texture width and texture height by 16 and subtract 1.
       The actual values to set are 1, 3, 7, 15, 31, 63. */
    PVR_SET(PVR_YUV_CFG, (((MPEG_TEXTURE_HEIGHT / 16) - 1) << 8) | 
                         ((MPEG_TEXTURE_WIDTH / 16) - 1));
    PVR_GET(PVR_YUV_CFG);

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, 
                    PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED, 
                    MPEG_TEXTURE_WIDTH, MPEG_TEXTURE_HEIGHT, 
                    player->texture, 
                    PVR_FILTER_BILINEAR);
    pvr_poly_compile(&player->hdr, &cxt);

    player->hdr.mode3 |= PVR_TXRFMT_STRIDE;

    u = (float)player->width / MPEG_TEXTURE_WIDTH;
    v = (float)player->height / MPEG_TEXTURE_HEIGHT;

    player->vert[0].x = 0.0f;
    player->vert[0].y = 0.0f;
    player->vert[0].z = 1.0f;
    player->vert[0].u = 0.0f;
    player->vert[0].v = 0.0f;
    player->vert[0].argb = color;
    player->vert[0].oargb = 0;
    player->vert[0].flags = PVR_CMD_VERTEX;

    player->vert[1].x = 640.0f;
    player->vert[1].y = 0.0f;
    player->vert[1].z = 1.0f;
    player->vert[1].u = u;
    player->vert[1].v = 0.0f;
    player->vert[1].argb = color;
    player->vert[1].oargb = 0;
    player->vert[1].flags = PVR_CMD_VERTEX;

    player->vert[2].x = 0.0f;
    player->vert[2].y = 480.0f;
    player->vert[2].z = 1.0f;
    player->vert[2].u = 0.0f;
    player->vert[2].v = v;
    player->vert[2].argb = color;
    player->vert[2].oargb = 0;
    player->vert[2].flags = PVR_CMD_VERTEX;

    player->vert[3].x = 640.0f;
    player->vert[3].y = 480.0f;
    player->vert[3].z = 1.0f;
    player->vert[3].u = u;
    player->vert[3].v = v;
    player->vert[3].argb = color;
    player->vert[3].oargb = 0;
    player->vert[3].flags = PVR_CMD_VERTEX_EOL;

    return 0;
}

static int setup_audio(mpeg_player_t *player) {
    player->snd_mod_size = 0;
    player->snd_mod_start = 0;
    player->audio_time = 0.0f;
    player->audio_interval = player->audio_time;

    player->snd_hnd = snd_stream_alloc(sound_callback, 0x10000);
    if(player->snd_hnd == SND_STREAM_INVALID) {
        return -1;
    }
    
    snd_stream_volume(player->snd_hnd, 0xff);
    snd_stream_set_userdata(player->snd_hnd, player);

    return 0;
}

int mpeg_play(mpeg_player_t *player, uint32_t buttons) {
    int cancel = 0;
    plm_frame_t *frame;
    int decoded;
    int samplerate;

    if(!player || !player->decoder)
        return -1;

    /* First frame */
    frame = plm_decode_video(player->decoder);
    decoded = 1;

    /* Init sound stream. */
    samplerate = plm_get_samplerate(player->decoder);
    snd_stream_start(player->snd_hnd, samplerate, 0);

    while(!cancel) {
        /* Check cancel buttons. */
        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
        if(buttons && ((st->buttons & buttons) == buttons))
            cancel = 1; /* Push cancel buttons */
        if(st->buttons == 0x60e)
            cancel = 2; /* ABXY + START (Software reset) */
        MAPLE_FOREACH_END()

        /* Decode */
        if((player->audio_time - player->audio_interval) >= frame->time) {
            frame = plm_decode_video(player->decoder);
            if(!frame)
                break;
            decoded = 1;
        }
        snd_stream_poll(player->snd_hnd);

        /* Render */
        pvr_wait_ready();
        pvr_scene_begin();

        if(decoded) {
            upload_frame(frame);
            decoded = 0;
        }

        pvr_list_begin(PVR_LIST_OP_POLY);

        draw_frame(player);

        pvr_list_finish();
        pvr_scene_finish();
    }

    return cancel;
}
