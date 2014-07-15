#
# http://www.gnu.org/software/make/manual/make.html
#
CC:=gcc
INCLUDES:=$(shell pkg-config --cflags libavformat libavcodec libswscale libavutil libswresample sdl)
CFLAGS:=
LDFLAGS:=$(shell pkg-config --libs libavformat libavcodec libswscale libavutil libswresample sdl) -lm
# EXE:=tutorial_1.out tutorial_2.out tutorial_4.out tutorial_4_1.out
EXE:=sdl_player

# $< is the first dependency in the dependency list
# $@ is the target name
#
$(EXE):player.c packet_queue.c audio_handler.c video_handler.c
	$(CC) $(CFLAGS) player.c packet_queue.c  audio_handler.c video_handler.c $(INCLUDES) $(LDFLAGS) -o $(EXE)

clean:
	rm sdl_player


