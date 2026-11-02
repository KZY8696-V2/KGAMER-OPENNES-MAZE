CC = cc65
AS = ca65
LD = ld65

all: nes famicom fds twin

nes:
	$(CC) -O -t nes main_nes.c -o main_nes.s
	$(AS) main_nes.s -o main_nes.o
	$(LD) -t nes main_nes.o -o kgamer_open_nes_maze_nes.nes

famicom:
	$(CC) -O -t nes main_famicom.c -o main_fc.s
	$(AS) main_fc.s -o main_fc.o
	$(LD) -t nes main_fc.o -o kgamer_open_nes_maze_famicom.nes

fds:
	$(CC) -O -t fds main_fds.c -o main_fds.s
	$(AS) main_fds.s -o main_fds.o
	$(LD) -t fds main_fds.o -o kgamer_open_nes_maze_fds.fds

twin:
	$(CC) -O -t nes main-fdstwin.c -o main_twin.s
	$(AS) main_twin.s -o main_twin.o
	$(LD) -t nes main_twin.o -o kgamer_open_nes_maze_twin.nes

clean:
	rm -f *.s *.o *.nes *.fds

.PHONY: all clean nes famicom fds twin
