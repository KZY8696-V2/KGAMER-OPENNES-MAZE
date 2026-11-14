CC = cc65
AS = ca65
LD = ld65

all: nes

nes:
	$(CC) -O -t nes main_nes.c -o main_nes.s
	$(AS) main_nes.s -o main_nes.o
	$(LD) -t nes main_nes.o -o kgamer_open_nes_maze_nes.nes

clean:
	rm -f *.s *.o *.nes

.PHONY: all clean nes
