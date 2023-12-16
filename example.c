#include <kos.h>
#include "mpeg.h"

int main(void) {
    pvr_init_defaults();
    snd_stream_init();

    mpeg_play("/rd/sample.mpg", CONT_START);

    return 0;
}
