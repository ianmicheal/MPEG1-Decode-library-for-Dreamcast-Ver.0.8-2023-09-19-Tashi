#include <kos.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "mpeg.h"

struct mpeg_player_t {
    plm_t *decoder;
    uint32_t *snd_buf;
    pvr_ptr_t texture;
    int width;
    int height;
    int snd_mod_start;
    int snd_mod_size;
    snd_stream_hnd_t snd_hnd;
    float audio_time;
    float audio_interval;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert[4];
};

/* Output texture width and height initial values
   You can choose from 32, 64, 128, 256, 512, 1024 */
#define MPEG_TEXTURE_WIDTH 512
#define MPEG_TEXTURE_HEIGHT 256

#define SOUND_BUFFER (64 * 1024)

static int setup_graphics(mpeg_player_t *player);
static int setup_audio(mpeg_player_t *player);
static void fast_memcpy(void *dest, const void *src, size_t length);

mpeg_player_t *mpeg_player_create(const char *filename) {
    mpeg_player_t *player = NULL;

    if(!filename) {
        fprintf(stderr, "Filename is NULL\n");
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
        mpeg_player_destroy(player);
        return NULL;
    }

    player->snd_buf = memalign(32, SOUND_BUFFER);
    if(!player->snd_buf) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    if(setup_graphics(player) < 0) {
        fprintf(stderr, "Setting up graphics failed\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    if(setup_audio(player) < 0) {
        fprintf(stderr, "Setting up audio failed\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    return player;
}

mpeg_player_t *mpeg_player_create_memory(uint8_t *memory, const size_t length) {
    mpeg_player_t *player = NULL;

    if(!memory) {
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
        mpeg_player_destroy(player);
        return NULL;
    }

    player->snd_buf = memalign(32, SOUND_BUFFER);
    if(!player->snd_buf) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    if(setup_graphics(player) < 0) {
        fprintf(stderr, "Setting up graphics failed\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    if(setup_audio(player) < 0) {
        fprintf(stderr, "Setting up audio failed\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    return player;
}

void mpeg_player_destroy(mpeg_player_t *player) {
    if(!player) {
        printf("Player is NULL\n");
        return;
    }

    if(player->decoder) {
        printf("Freed decoder\n");
        plm_destroy(player->decoder);
    }

    if(player->texture) {
        printf("Freed texture memory\n");
        pvr_mem_free(player->texture);
    }

    if(player->snd_buf) {
        printf("Freed sound buffer\n");
        free(player->snd_buf);
    }

    if(player->snd_hnd != SND_STREAM_INVALID) {
        printf("Freed stream handle\n");
        snd_stream_destroy(player->snd_hnd);
    }

    free(player);
    player = NULL;
    printf("Freed player\n");
}

static void *sound_callback(snd_stream_hnd_t hnd, int size, int *size_out) {
    plm_samples_t *sample;
    mpeg_player_t *player = snd_stream_get_userdata(hnd);
    uint32_t *dest = player->snd_buf;

    if(player->snd_mod_size > 0) {
        fast_memcpy(dest, player->snd_buf + player->snd_mod_start / 4, player->snd_mod_size);
    }

    int out = player->snd_mod_size;

    while(size > out) {
        sample = plm_decode_audio(player->decoder);
        if (!sample) {
            player->audio_time += player->audio_interval;
            break;
        }
        player->audio_time = sample->time;
        fast_memcpy(dest + out / 4, sample->pcm, 1152 * 2);
        out += 1152 * 2;
    }

    player->snd_mod_start = size;
    player->snd_mod_size = out - size;
    *size_out = size;

    return player->snd_buf;
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

    for(y = 0; y < h; y++) {
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
    player->width = plm_get_width(player->decoder);
    player->height = plm_get_height(player->decoder);

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

static __attribute__((noinline)) void fast_memcpy(void *dest, const void *src, size_t length) {
    int blocks;
    int remainder;
    char *char_dest = (char *)dest;
    const char *char_src = (const char *)src;

    _Complex float ds;
    _Complex float ds2;
    _Complex float ds3;
    _Complex float ds4;

    if (((uintptr_t)dest | (uintptr_t)src) & 7) {
        memcpy(dest, src, length);
    }
    else { /* Fast Path */
        blocks = length / 32;
        remainder = length % 32;

        if(blocks > 0) {
            __asm__ __volatile__ (
                "fschg\n\t"
                "clrs\n" 
                ".align 2\n"
                "1:\n\t"
                /* *dest++ = *src++ */
                "fmov.d @%[in]+, %[scratch]\n\t"
                "fmov.d @%[in]+, %[scratch2]\n\t"
                "fmov.d @%[in]+, %[scratch3]\n\t"
                "fmov.d @%[in]+, %[scratch4]\n\t"
                "movca.l %[r0], @%[out]\n\t"
                "add #32, %[out]\n\t"
                "pref @%[in]\n\t"  /* Prefetch 32 bytes for next loop */
                "dt %[blocks]\n\t"   /* while(blocks--) */
                "fmov.d %[scratch4], @-%[out]\n\t"
                "fmov.d %[scratch3], @-%[out]\n\t"
                "fmov.d %[scratch2], @-%[out]\n\t"
                "fmov.d %[scratch], @-%[out]\n\t"
                "bf.s 1b\n\t"
                "add #32, %[out]\n\t"
                "fschg\n"
                : [in] "+&r" ((uintptr_t)src), [out] "+&r" ((uintptr_t)dest), 
                [blocks] "+&r" (blocks), [scratch] "=&d" (ds), [scratch2] "=&d" (ds2), 
                [scratch3] "=&d" (ds3), [scratch4] "=&d" (ds4) /* outputs */
                : [r0] "z" (remainder) /* inputs */
                : "t", "memory" /* clobbers */
            );
        }

        while(remainder--) {
            *char_dest++ = *char_src++;
        }
    }
}

static int setup_audio(mpeg_player_t *player) {
    player->snd_mod_size = 0;
    player->snd_mod_start = 0;
    player->audio_time = 0.0f;
    player->audio_interval = player->audio_time;

    player->snd_hnd = snd_stream_alloc(sound_callback, SOUND_BUFFER);
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

    if (!player || !player->decoder)
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
        if (buttons && ((st->buttons & buttons) == buttons))
            cancel = 1; /* Push cancel buttons */
        if (st->buttons == 0x60e)
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
