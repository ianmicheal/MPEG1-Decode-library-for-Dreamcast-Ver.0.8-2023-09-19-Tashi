#include <kos.h>
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "mpeg1.h"
#include "profiler.h"
static plm_t *plm;

/* textures */
static pvr_ptr_t texture;
static int width, height;
// Output texture width and height initial values
// You can choose from 32, 64, 128, 256, 512, 1024
#define MPEG1_TEXTURE_WIDTH 512
#define MPEG1_TEXTURE_HEIGHT 256

void *shcMemSet( void *dest, int c, size_t count )
{
#if 0
	U8	*ptr,*sq,*high;
	S32	nb;

	ptr = (U8*)dest;

	ptr = (U8*)(((U32)ptr+31)&~(31))
	/* align dest to 32 bytes */
/*
	nb = (((U32)ptr)-(((U32)ptr)&~(31)));
	while( nb>0 )
	{
		*ptr++ = c;
		if( !(--count) )	return dest;
		nb--;
	}
*/

	/* fill in store queue */

	sq = (U8*)0xE0000000;
	for( nb=64; nb>0; nb-- )	*sq++ = c;


	/* set dest image to store queue */
	ptr = (U8*)( (((U32)ptr)&0x03FFFFFF) | 0xE0000000 );

	while( count>=32 )
	{
		Prefetch( ptr );
		count-=32;
		ptr+=32;
	}

/*	ptr = (U8*)((((U32)dest)&0xFC000000)|(((U32)ptr)&0x03FFFFFF));*/

	/* copy last bytes */
/*	while( count )
	{
		*ptr++ = c;
		count--;
	}
*/
#endif
	return dest;
}

snd_stream_hnd_t snd_hnd;
__attribute__((aligned(32))) unsigned int snd_buf[0x10000 / 4];

// Structure to represent a video frame
typedef struct {
    plm_frame_t *frame;
} VideoFrame;
//Ian micheal Implemented a ringbuffer version
// Ring buffer to hold video frames
#define RING_BUFFER_SIZE 20
static VideoFrame ringBuffer[RING_BUFFER_SIZE];
static int ringBufferHead = 0;
static int ringBufferTail = 0;

// Function to initialize the ring buffer
void initRingBuffer() {
    ringBufferHead = 0;
    ringBufferTail = 0;
    memset(ringBuffer, 0, sizeof(ringBuffer));
}

// Function to enqueue a video frame into the ring buffer
void enqueueFrame(plm_frame_t *frame) {
    if ((ringBufferHead + 1) % RING_BUFFER_SIZE != ringBufferTail) {
        ringBuffer[ringBufferHead].frame = frame;
        ringBufferHead = (ringBufferHead + 1) % RING_BUFFER_SIZE;
    }
}

// Function to dequeue a video frame from the ring buffer
plm_frame_t *dequeueFrame() {
    plm_frame_t *frame = NULL;
    if (ringBufferHead != ringBufferTail) {
        frame = ringBuffer[ringBufferTail].frame;
        ringBufferTail = (ringBufferTail + 1) % RING_BUFFER_SIZE;
    }
    return frame;
}
#define UV_EPSILON 0.046f  // Adjust the value as needed
#define screenWidth 640
#define screenHeight 480
void display_draw(void)
{
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;
    float u = (float)width / (float)MPEG1_TEXTURE_WIDTH;
    float v = (float)height / (float)MPEG1_TEXTURE_HEIGHT;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED, MPEG1_TEXTURE_WIDTH, MPEG1_TEXTURE_HEIGHT, texture, PVR_FILTER_NEAREST);
    pvr_poly_compile(&hdr, &cxt);

    // hdr.mode3 |= 0x02000000; /* stride */
    pvr_prim(&hdr, sizeof(hdr));

    vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert.oargb = 0;
    vert.flags = PVR_CMD_VERTEX;

    vert.x = 1;
    vert.y = 1;
    vert.z = 1;
    vert.u = UV_EPSILON;
    vert.v = UV_EPSILON;
    pvr_prim(&vert, sizeof(vert));

    vert.x = screenWidth;
    vert.y = 1;
    vert.z = 1;
    vert.u = u - UV_EPSILON;
    vert.v = UV_EPSILON;
    pvr_prim(&vert, sizeof(vert));

    vert.x = 1;
    vert.y = screenHeight;
    vert.z = 1;
    vert.u = UV_EPSILON;
    vert.v = v - UV_EPSILON;
    pvr_prim(&vert, sizeof(vert));

    vert.x = screenWidth;
    vert.y = screenHeight;
    vert.z = 1;
    vert.u = u - UV_EPSILON;
    vert.v = v - UV_EPSILON;
    vert.flags = PVR_CMD_VERTEX_EOL;
    pvr_prim(&vert, sizeof(vert));
}


// Ian micheal optimized SQ function
void bit64_sq_cpy(void *dest, void *src, int n)
{
  uint32 *sq;
  uint32 *d, *s;
  // Cast the destination pointer to uint32* and set it to the base address
  // of a specific memory region (0xe0000000) with some bit manipulation.
  d = (uint32 *)(0xe0000000 | (((uint32)dest) & 0x03ffffe0));
 // Cast the source pointer to uint32*.
  s = (uint32 *)(src);
  // Set specific memory-mapped registers to configure memory access.
  *((volatile unsigned int*)0xFF000038) = ((((uint32)dest) >> 26) << 2) & 0x1c;
  *((volatile unsigned int*)0xFF00003C) = ((((uint32)dest) >> 26) << 2) & 0x1c;

  // Right shift 'n' by 6 (equivalent to dividing by 64) to determine the number of 64-byte blocks to copy.
  n >>= 6;
  while (n--) 
  {
    // Copy 64 bytes (8 uint32 values) from source to destination.
    // sq0
    sq = d;
    *sq++ = *s++; *sq++ = *s++;
    *sq++ = *s++; *sq++ = *s++;
    *sq++ = *s++; *sq++ = *s++;
    *sq++ = *s++; *sq++ = *s++;
    // Issue a prefetch operation for the destination memory region.
    __asm__("pref @%0" : : "r" (d));
    __asm__("ocbi @r4" : : "r" (dest));  // Issue a cache operation to write the destination memory region (ocbi = "orderly cache block invalidate"). 
    // Move the destination pointer by 8 uint32 values (64 bytes).
    d += 8;
    // sq1
    sq = d;
    *sq++ = *s++; *sq++ = *s++;
    *sq++ = *s++; *sq++ = *s++;
    *sq++ = *s++; *sq++ = *s++;
    *sq++ = *s++; *sq++ = *s++;
    // Issue a prefetch operation for the next destination memory region.
    __asm__("pref @%0" : : "r" (d));
    // Issue a cache operation to write the next destination memory region.
    __asm__("ocbi @r4" : : "r" (dest));  
    // Move the destination pointer by 8 uint32 values (64 bytes) for the next iteration.
    d += 8;
  }

  // Clear specific memory-mapped registers.
  *((uint32 *)(0xe0000000)) = 0;
  *((uint32 *)(0xe0000020)) = 0;
}

/* Ian micheal Waiting for both queues can introduce unnecessary delays 
Fills n bytes at s with int c, s must be 32-byte aligned */
void *sq_set_32_imr(void *s, uint32 c, int n) {
    unsigned int *d = (unsigned int *)(void *)
                      (0xe0000000 | (((unsigned long)s) & 0x03ffffe0));

    /* Set store queue memory area as desired */
    QACR0 = ((((unsigned int)s) >> 26) << 2) & 0x1c;
    QACR1 = ((((unsigned int)s) >> 26) << 2) & 0x1c;

    /* Fill both store queues with c */
    d[0] = d[1] = d[2] = d[3] = d[4] = d[5] = d[6] = d[7] =
                                           d[8] = d[9] = d[10] = d[11] = d[12] = d[13] = d[14] = d[15] = c;

    /* Issue cache operations to write the data asynchronously */
    __asm__("ocbi @r4" : : "r"(s));

    /* Adjust the pointer for the next operation */
    s += 32;

    /* Write them as many times necessary */
    n >>= 6;

    while (n--) {
        /* Issue cache operations for the next block of data */
        __asm__("ocbi @r4" : : "r"(s));
        s += 32;
    }

    return s;
}
/* Fills n bytes at s with byte c, s must be 32-byte aligned */
void *sq_set_imr(void *s, uint32 c, int n) {
    unsigned int *d = (unsigned int *)(void *)
                      (0xe0000000 | (((unsigned long)s) & 0x03ffffe0));

    /* Set store queue memory area as desired */
    QACR0 = ((((unsigned int)s) >> 26) << 2) & 0x1c;
    QACR1 = ((((unsigned int)s) >> 26) << 2) & 0x1c;

    /* Duplicate low 8-bits of c into high 24-bits */
    c = c & 0xff;
    c = (c << 24) | (c << 16) | (c << 8) | c;

    /* Fill/write queues as many times necessary */
    n >>= 5;

    while (n--) {
        /* Issue cache operations to write the data asynchronously */
        __asm__("ocbi @%0" : : "r"(s));

        /* Prefetch 32 bytes for the next loop */
        __asm__("pref @%0" : : "r"(d));

        /* Fill 32 bytes (8 uint32 values) with c */
        d[0] = c;
        d[1] = c;
        d[2] = c;
        d[3] = c;
        d[4] = c;
        d[5] = c;
        d[6] = c;
        d[7] = c;

        /* Move the destination pointer by 8 uint32 values (32 bytes) for the next iteration */
        d += 8;
        s += 32;
    }

    return s;
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
            bit64_sq_cpy((void *)0x10800000, (void *)src, 384);
        }
        if (!stride)
        {
            /* Send dummy mb */
            for (i = 0; i < 32 - w; i++)
            {
                sq_set_imr((void *)0x10800000, 0, 384);
            }
        }
    }
    for (i = 0; i < 16 - h; i++)
    {
        if (!stride)
            sq_set_imr((void *)0x10800000, 0, 384 * 32);
        else
            sq_set_imr((void *)0x10800000, 0, 384 * w);
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
// Sound callback function
void *sound_callback(snd_stream_hnd_t hnd, int size, int *size_out)
{
    unsigned int *dest = audio_sample_buffer;
   unsigned int *src = NULL; // Initialize src pointer
    int out = 0;

    // Prefetch the initial 'src' address
    __asm__("pref @%0" : : "r" (src));

    src = audio_sample_buffer + mod_start / 4;
    for (int i = 0; i < mod_size / 4; i++) {
        *dest++ = *src++;
        // Prefetch the next memory location (adjust the distance as needed)
        __asm__("pref @%0" : : "r" (src));
    }
    out += mod_size;

    while (size > out)
    {
        plm_samples_t *sample = plm_decode_audio(plm);

        if (sample == NULL)
        {
            reset_sound_callback(); // Reset static variables when audio ends
            break;
        }

        // Prefetch the initial 'src' address for the audio samples
        __asm__("pref @%0" : : "r" (src));

        src = (unsigned int *)sample->pcm;
        for (int i = 0; i < 1152 / 2; i++) {
            *dest++ = *src++;
            // Prefetch the next memory location (adjust the distance as needed)
            __asm__("pref @%0" : : "r" (src));
        }
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

    // Initialize the ring buffer
    initRingBuffer();

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
    /* Check cancel buttons. */
MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
if (buttons && ((st->buttons & buttons) == buttons))
   cancel = 1; /* Push cancel buttons */
if (st->buttons == 0x60e)
   cancel = 2; /* ABXY + START (Software reset) */
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
            // Enqueue the frame into the ring buffer
            enqueueFrame(frame);
        }
        snd_stream_poll(snd_hnd);

        /* Render */
        pvr_wait_ready();
        pvr_scene_begin();
        if (decoded)
        {
            frame = dequeueFrame();
            if (frame) {
                app_on_video(plm, frame, 0);
            }
            decoded = 0;
        }
        pvr_list_begin(PVR_LIST_OP_POLY);
        display_draw();
        pvr_list_finish();
        pvr_scene_finish();
    }
  //  profiler_stop();
  //  profiler_clean_up();
     //Ian micheal added call back sound reset and buffer
    thd_sleep(1000);
    printf("Exiting main()\n");
    reset_sound_callback();
    printf("reset call back sound()\n");
    plm_destroy(plm);
    printf("plm_destroy()\n");
    pvr_mem_free(texture);
    printf("pvr_mem_free\n");
    snd_stream_destroy(snd_hnd);
    printf("snd_stream_destroy\n");
    pvr_shutdown(); // Clean up PVR resources
    vid_shutdown(); // This function reinitializes the video system to what dcload and friends expect it to be

    printf("arch_exit\n");
    return cancel;
}

