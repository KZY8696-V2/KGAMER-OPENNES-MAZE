# cc65 Araç Takımı Tanımları
CC = cc65
AS = ca65
LD = ld65

# Derleme Bayrakları (-O optimizasyon, -t nes hedef platform)
CFLAGS = -O -t nes

# Çıktı ROM Dosyası Adı
TARGET = kgamer_open_nes_maze.nes

all: $(TARGET)

# C kodunu Assembly dosyasına çevir
%.s: %.c
	$(CC) $(CFLAGS) $<

# Assembly dosyasını Nes objesine derle
%.o: %.s
	$(AS) $< -o $@

# Nes objesini NES ROM formatına bağla (Link)
$(TARGET): main.o
	$(LD) -t nes main.o -o $(TARGET)

# Geçici dosyaları ve ROM'u temizle
clean:
	rm -f *.s *.o $(TARGET)

.PHONY: all clean
