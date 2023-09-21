# PL_MPEG - MPEG1 Video decoder, MP2 Audio decoder, MPEG-PS demuxer

MPEG1 Decode library for Dreamcast Ver.0.8
	2023/09/19 Tashi


#### FEATURES ####
You can play MPEG1 videos with audio.
Audio is monaural only. For stereo, only the left channel will be played.
You can specify a cancel button during playback.
The recommended resolutions are:
	4:3 = 320x240 Mono audio 80kbits
	16:9 = 368x208 Mono audio 80kbits


#### LICENSE ####
pl_mpeg.h - MIT LICENSE
mpeg1.c, mpeg1.h - Public Domain


#### THANKS TO ####
Dominic Szablewski (https://phoboslab.org) - Great decoding engine.
Ian Robinson & BB Hood - They provided advice and ideas.

with permission https://dcemulation.org/phpBB/viewtopic.php?p=1060259#p1060259

I have added this here to get your started with encoding for a cdr
ffmpeg -i input.mp4 -vf "scale=320:240" -b:v 800k -ac 1 -ar 44100 -b:a 80k -f mpeg output.mpeg

Single-file MIT licensed library for C/C++

See [pl_mpeg.h](https://github.com/phoboslab/pl_mpeg/blob/master/pl_mpeg.h) for
the documentation.


## Why?

This is meant as a simple way to get video playback into your app or game. Other
solutions, such as ffmpeg require huge libraries and a lot of glue code.

MPEG1 is an old and inefficient codec, but it's still good enough for many use
cases. All patents related to MPEG1 and MP2 have expired, so it's completely
free now.

This library does not make use of any SIMD instructions, but because of
the relative simplicity of the codec it still manages to decode 4k60fps video
on a single CPU core (on my i7-6700k at least).


## Example Usage

- [pl_mpeg_extract_frames.c](https://github.com/phoboslab/pl_mpeg/blob/master/pl_mpeg_extract_frames.c)
extracts all frames from a video and saves them as PNG.
 - [pl_mpeg_player.c](https://github.com/phoboslab/pl_mpeg/blob/master/pl_mpeg_player.c)
implements a video player using SDL2 and OpenGL for rendering.



## Encoding for PL_MPEG

Most [MPEG-PS](https://en.wikipedia.org/wiki/MPEG_program_stream) (`.mpg`) files
containing MPEG1 Video ("mpeg1") and MPEG1 Audio Layer II ("mp2") streams should
work with PL_MPEG. Note that `.mpg` files can also contain MPEG2 Video, which is
not supported by this library.

You can encode video in a suitable format using ffmpeg:

```
ffmpeg -i input.mp4 -c:v mpeg1video -c:a mp2 -format mpeg output.mpg
```

If you just want to quickly test the library, try this file:

https://phoboslab.org/files/bjork-all-is-full-of-love.mpg


## Limitations

- no error reporting. PL_MPEG will silently ignore any invalid data.
- the pts (presentation time stamp) for packets in the MPEG-PS container is
ignored. This may cause sync issues with some files.
- no seeking.
- bugs, probably.
