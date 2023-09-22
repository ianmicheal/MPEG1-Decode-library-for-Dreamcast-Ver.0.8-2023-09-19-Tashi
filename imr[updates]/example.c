#include <kos.h>
#include <string.h>
#include <stdio.h>
#include <arch/arch.h>
#include "mpeg1.h"

/* romdisk */
extern uint8 romdisk_boot[];
KOS_INIT_ROMDISK(romdisk_boot);



int main(void)
{

    pvr_init_defaults();
    snd_stream_init();

    Mpeg1Play("/cd/zample.mpg", CONT_START);
    //Ian micheal now exits at the end of video
    snd_stream_shutdown();
    spu_disable(); // Disable the Sound Processing Unit (SPU).
    arch_exit(); // Perform system-specific exit operations.
    return 0;
}
