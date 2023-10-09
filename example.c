#include <kos.h>
#include <string.h>
#include <stdio.h>
#include <arch/arch.h>
#include "mpeg1.h"
#include "profiler.h"

/* romdisk */
extern uint8 romdisk_boot[];
KOS_INIT_ROMDISK(romdisk_boot);



int main(void)
{
  // profiler_init("/pc/gmon.out");
 //  profiler_start();

    pvr_init_defaults();
    snd_stream_init();

    Mpeg1Play("/cd/zvideo/z.mpg", CONT_START);
  
  

    return 0;
}
