#include <kos.h>
#include "mpeg.h"

int main(void) {
    pvr_init_defaults();
    snd_stream_init();

    mpeg_player_t *player;

    player = mpeg_player_create("/rd/sample.mpg");

    mpeg_play(player, CONT_START);

    return 0;
}
