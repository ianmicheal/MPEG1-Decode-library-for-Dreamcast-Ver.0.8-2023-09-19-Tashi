#include <kos.h>
#include "mpeg1.h"

/* romdisk */
extern uint8 romdisk_boot[];
KOS_INIT_ROMDISK(romdisk_boot);

int main(void)
{
    pvr_init_defaults();
    snd_stream_init();

    Mpeg1Play("/rd/sample.mpg", CONT_START);

    snd_stream_shutdown();
    return 0;
}
