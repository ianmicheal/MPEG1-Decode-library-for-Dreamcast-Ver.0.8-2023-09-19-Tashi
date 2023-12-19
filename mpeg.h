#ifndef _MPEG_H_INCLUDED_
#define _MPEG_H_INCLUDED_

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

typedef struct mpeg_player_t mpeg_player_t;

mpeg_player_t *mpeg_player_create(const char *filename);
mpeg_player_t *mpeg_player_create_memory(unsigned char *memory, const size_t length);

void mpeg_player_destroy(mpeg_player_t *player);

int mpeg_play(mpeg_player_t *player, uint32_t buttons);

#ifdef __cplusplus
}
#endif

#endif
