all: rm-elf example.elf



KOS_CFLAGS+= -std=c99  -fstack-protector
include $(KOS_BASE)/Makefile.rules

OBJS = example.o mpeg1.o

KOS_LOCAL_CFLAGS = -I$(KOS_BASE)/addons/zlib
	
clean:
	-rm -f example.elf $(OBJS)
	-rm -f romdisk_boot.*

dist:
	-rm -f $(OBJS)
	-rm -f romdisk_boot.*
	$(KOS_STRIP) example.elf
	
rm-elf:
	-rm -f example.elf
	-rm -f romdisk_boot.*

example.elf: $(OBJS) romdisk_boot.o 
	$(KOS_CC) $(KOS_CFLAGS) $(KOS_LDFLAGS) -o $@ $(KOS_START) $^ -lz -lm $(KOS_LIBS)

romdisk_boot.img:
	$(KOS_GENROMFS) -f $@ -d romdisk_boot -v

romdisk_boot.o: romdisk_boot.img
	$(KOS_BASE)/utils/bin2o/bin2o $< romdisk_boot $@

run: example.elf
	$(KOS_LOADER) $<

