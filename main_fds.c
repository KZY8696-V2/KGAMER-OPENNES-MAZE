#include <nes.h>
#include <stdint.h>
#include <string.h>

/* Oyun Durumları (State Machine) */
#define STATE_MAIN_MENU  0
#define STATE_PLAYING    1
#define STATE_PAUSE      2
#define STATE_GAMEOVER   3

uint8_t game_state = STATE_MAIN_MENU;
uint8_t menu_selection = 0;

/* Yapılandırma Veri Yapısı (FDS RAM Disk Uyumlu) */
typedef struct {
    uint8_t difficulty;       // 0: Easy, 1: Medium, 2: Hard
    uint8_t character_type;   // 0: Lizard ('L'), 1: Snake ('S')
    uint8_t maze_render_mode; // 0: GFX (Çizim), 1: TEXT (# / .)
    uint8_t char_render_mode; // 0: GFX (Çizim), 1: TEXT (L / S)
    uint8_t timer_mode;       // 0: Timeless, 1: Timed
    uint8_t no_color_mode;    // 0: Color, 1: B&W
    uint8_t save_target;      // 0: FDS RAM Disk, 1: BIOS Defaults
} GameSettings;

/* FDS Dahili Varsayılan Ayarları */
const GameSettings FDS_DEFAULTS = {
    1, // Medium Difficulty
    0, // Lizard
    0, // GFX Maze
    0, // GFX Character
    1, // Timed Mode
    0, // Color
    0  // FDS RAM Disk Storage
};

GameSettings current_settings;

uint16_t time_left = 60;
uint16_t frame_counter = 0;
uint8_t current_round = 1;

/* CHR VRAM Grafik İndeksleri */
#define TILE_WALL       0x01
#define TILE_PATH       0x02
#define TILE_LIZARD_GFX 0x03
#define TILE_SNAKE_GFX  0x04
#define TILE_EXIT       0x05

/* Labirent Boyutları */
#define MAX_MAZE_W 31
#define MAX_MAZE_H 25
uint8_t maze_w = 21;
uint8_t maze_h = 19;
char maze[MAX_MAZE_H][MAX_MAZE_W];

uint8_t p_x = 1, p_y = 1;
uint8_t e_x = 19, e_y = 17;

/* LCG Rastgele Üreteç */
static uint16_t lcg_seed = 1986;
uint16_t next_rand(void) {
    lcg_seed = (lcg_seed * 25173 + 13849 + current_round) % 65535;
    return lcg_seed;
}

/* Renk Paleti Havuzu (FDS Disk Uyumlu Tonlar) */
typedef struct {
    uint8_t wall;
    uint8_t path;
    uint8_t accent;
} PaletteConfig;

const PaletteConfig PALETTE_POOL[] = {
    {0x16, 0x30, 0x27},
    {0x12, 0x30, 0x21},
    {0x1A, 0x30, 0x28},
    {0x14, 0x30, 0x34},
    {0x27, 0x30, 0x18}
};
#define POOL_SIZE 5

uint8_t active_pal = 0;
uint8_t last_pal = 0xFF;

/* FDS RAM Disk / BIOS Yöneticisi ($6000 - $600F) */
void save_settings(void) {
    volatile uint8_t* fds_magic = (volatile uint8_t*)0x6000;
    volatile uint8_t* fds_data = (volatile uint8_t*)0x6001;

    if (current_settings.save_target == 0) {
        fds_magic[0] = 'F';
        fds_magic[1] = 'D';
        memcpy((void*)fds_data, &current_settings, sizeof(GameSettings));
    } else {
        fds_magic[0] = 0x00;
        fds_magic[1] = 0x00;
    }
}

void load_settings(void) {
    volatile uint8_t* fds_magic = (volatile uint8_t*)0x6000;
    volatile uint8_t* fds_data = (volatile uint8_t*)0x6001;

    if (fds_magic[0] == 'F' && fds_magic[1] == 'D') {
        memcpy(&current_settings, (void*)fds_data, sizeof(GameSettings));
    } else {
        memcpy(&current_settings, &FDS_DEFAULTS, sizeof(GameSettings));
    }
}

/* PPU Temizleme ve Yazma Yardımcıları */
void ppu_clear_screen(void) {
    uint16_t i;
    PPU.addr = 0x20;
    PPU.addr = 0x00;
    for (i = 0; i < 1024; i++) {
        PPU.data = 0x00;
    }
}

void ppu_write_str(uint16_t addr, const char* str) {
    PPU.addr = (uint8_t)(addr >> 8);
    PPU.addr = (uint8_t)(addr & 0xFF);
    while (*str) {
        PPU.data = *str - 0x20;
        str++;
    }
}

void ppu_put_tile(uint16_t addr, uint8_t tile) {
    PPU.addr = (uint8_t)(addr >> 8);
    PPU.addr = (uint8_t)(addr & 0xFF);
    PPU.data = tile;
}

void apply_palette(void) {
    uint8_t new_pal;
    PPU.addr = 0x3F;
    PPU.addr = 0x00;
    
    if (current_settings.no_color_mode) {
        PPU.data = 0x0F;
        PPU.data = 0x30;
        PPU.data = 0x10;
        PPU.data = 0x20;
    } else {
        do {
            new_pal = (uint8_t)(next_rand() % POOL_SIZE);
        } while (new_pal == last_pal);
        
        active_pal = new_pal;
        last_pal = active_pal;
        
        PPU.data = 0x0F;
        PPU.data = PALETTE_POOL[active_pal].wall;
        PPU.data = PALETTE_POOL[active_pal].path;
        PPU.data = PALETTE_POOL[active_pal].accent;
    }
}

/* FDS Kontrolcü Okuma */
uint8_t read_controller(void) {
    uint8_t res = 0;
    uint8_t i;
    JOYPAD1 = 0x01;
    JOYPAD1 = 0x00;
    for (i = 0; i < 8; i++) {
        res >>= 1;
        if (JOYPAD1 & 0x01) res |= 0x80;
    }
    return res;
}

/* %100 Çözülebilir Rastgele Labirent (Randomized DFS) */
void generate_maze_world(void) {
    int16_t r, c, i;
    
    if (current_settings.difficulty == 0) {       
        maze_w = 15; maze_h = 13; if (current_settings.timer_mode) time_left = 45;
    } else if (current_settings.difficulty == 1) { 
        maze_w = 21; maze_h = 19; if (current_settings.timer_mode) time_left = 75;
    } else {                                      
        maze_w = 29; maze_h = 25; if (current_settings.timer_mode) time_left = 120;
    }

    for (r = 0; r < maze_h; r++) {
        for (c = 0; c < maze_w; c++) {
            maze[r][c] = '#';
        }
    }

    p_x = 1; p_y = 1;
    maze[p_y][p_x] = '.';

    {
        uint8_t stack_x[256], stack_y[256];
        int16_t top = 0;
        
        stack_x[top] = p_x;
        stack_y[top] = p_y;

        while (top >= 0) {
            uint8_t cx = stack_x[top];
            uint8_t cy = stack_y[top];
            
            int8_t dirs[4][2] = { {0, -2}, {0, 2}, {-2, 0}, {2, 0} };
            uint8_t found_unvisited = 0;
            
            for (i = 0; i < 4; i++) {
                uint8_t r_idx = (uint8_t)(next_rand() % 4);
                int8_t temp_r = dirs[0][0]; int8_t temp_c = dirs[0][1];
                dirs[0][0] = dirs[r_idx][0]; dirs[0][1] = dirs[r_idx][1];
                dirs[r_idx][0] = temp_r; dirs[r_idx][1] = temp_c;
            }

            for (i = 0; i < 4; i++) {
                int16_t nx = cx + dirs[i][0];
                int16_t ny = cy + dirs[i][1];
                
                if (nx > 0 && nx < maze_w - 1 && ny > 0 && ny < maze_h - 1) {
                    if (maze[ny][nx] == '#') {
                        maze[cy + (dirs[i][1] / 2)][cx + (dirs[i][0] / 2)] = '.';
                        maze[ny][nx] = '.';
                        
                        top++;
                        stack_x[top] = (uint8_t)nx;
                        stack_y[top] = (uint8_t)ny;
                        found_unvisited = 1;
                        break;
                    }
                }
            }
            if (!found_unvisited) {
                top--;
            }
        }
    }

    p_x = 1; p_y = 1;
    e_x = maze_w - 2; e_y = maze_h - 2;
    if (maze[e_y][e_x] == '#') e_x = maze_w - 3;
    
    {
        char player_sym = (current_settings.character_type == 0) ? 'L' : 'S';
        maze[p_y][p_x] = player_sym;
        maze[e_y][e_x] = 'O';
    }
}

/* Oyun Ekranı Çizimi ve Merkezleme */
void render_game_screen(void) {
    uint8_t r, c;
    uint16_t start_addr = 0x2080 + ((32 - maze_w) / 2);
    
    PPU.ctrl = 0;
    PPU.mask = 0;
    
    apply_palette();
    ppu_clear_screen();
    
    ppu_write_str(0x2022, "KGAMER FDS DISK MAZE");
    
    for (r = 0; r < maze_h; r++) {
        for (c = 0; c < maze_w; c++) {
            uint16_t addr = start_addr + (r * 32) + c;
            char cell = maze[r][c];
            
            if (r == p_y && c == p_x) {
                if (current_settings.char_render_mode == 0) {
                    ppu_put_tile(addr, (current_settings.character_type == 0) ? TILE_LIZARD_GFX : TILE_SNAKE_GFX);
                } else {
                    ppu_put_tile(addr, cell - 0x20);
                }
            } else if (r == e_y && c == e_x) {
                ppu_put_tile(addr, (current_settings.maze_render_mode == 0) ? TILE_EXIT : ('O' - 0x20));
            } else if (cell == '#') {
                ppu_put_tile(addr, (current_settings.maze_render_mode == 0) ? TILE_WALL : ('#' - 0x20));
            } else {
                ppu_put_tile(addr, (current_settings.maze_render_mode == 0) ? TILE_PATH : ('.' - 0x20));
            }
        }
    }
    
    PPU.ctrl = 0x90;
    PPU.mask = 0x1E;
}

/* Ana Menü Ekranı */
void render_main_menu(void) {
    PPU.ctrl = 0;
    PPU.mask = 0;
    ppu_clear_screen();
    
    ppu_write_str(0x2042, "== KGAMER FDS DISK SYSTEM ==");
    ppu_write_str(0x2086, menu_selection == 0 ? "> START DISK"   : "  START DISK");
    ppu_write_str(0x20A6, menu_selection == 1 ? "> DIFFICULTY: " : "  DIFFICULTY: ");
    ppu_write_str(0x20E6, menu_selection == 2 ? "> CHARACTER: "  : "  CHARACTER: ");
    ppu_write_str(0x2126, menu_selection == 3 ? "> MAZE MODE: "  : "  MAZE MODE: ");
    ppu_write_str(0x2166, menu_selection == 4 ? "> CHAR MODE: "  : "  CHAR MODE: ");
    ppu_write_str(0x21A6, menu_selection == 5 ? "> COLOR MODE: " : "  COLOR MODE: ");
    ppu_write_str(0x21E6, menu_selection == 6 ? "> FDS TARGET: " : "  FDS TARGET: ");
    ppu_write_str(0x2222, "D-PAD: NAV | A: CHANGE/SEL");
    
    ppu_write_str(0x20AA, current_settings.difficulty == 0 ? "EASY" : (current_settings.difficulty == 1 ? "MED " : "HARD"));
    ppu_write_str(0x20EA, current_settings.character_type == 0 ? "LIZARD" : "SNAKE ");
    ppu_write_str(0x212A, current_settings.maze_render_mode == 0 ? "GFX" : "TEXT");
    ppu_write_str(0x216A, current_settings.char_render_mode == 0 ? "GFX" : "TEXT");
    ppu_write_str(0x21AA, current_settings.no_color_mode ? "B&W" : "COL");
    ppu_write_str(0x21EA, current_settings.save_target == 0 ? "RAMDISK" : "BIOS");
    
    PPU.ctrl = 0x90;
    PPU.mask = 0x1E;
}

/* cc65 Giriş Noktası (main) */
void main(void) {
    uint8_t pad, last_pad = 0;
    
    load_settings();
    render_main_menu();
    
    while (1) {
        while (!(PPU.status & 0x80)); // VBlank Bekle
        
        pad = read_controller();
        
        if (game_state == STATE_MAIN_MENU) {
            if ((pad & 0x08) && !(last_pad & 0x08)) {
                if (menu_selection > 0) menu_selection--;
                render_main_menu();
            }
            if ((pad & 0x04) && !(last_pad & 0x04)) {
                if (menu_selection < 6) menu_selection++;
                render_main_menu();
            }
            if ((pad & 0x80) && !(last_pad & 0x80)) {
                if (menu_selection == 0) {
                    generate_maze_world();
                    render_game_screen();
                    game_state = STATE_PLAYING;
                } else if (menu_selection == 1) {
                    current_settings.difficulty = (current_settings.difficulty + 1) % 3;
                    save_settings();
                    render_main_menu();
                } else if (menu_selection == 2) {
                    current_settings.character_type = (current_settings.character_type + 1) % 2;
                    save_settings();
                    render_main_menu();
                } else if (menu_selection == 3) {
                    current_settings.maze_render_mode = (current_settings.maze_render_mode + 1) % 2;
                    save_settings();
                    render_main_menu();
                } else if (menu_selection == 4) {
                    current_settings.char_render_mode = (current_settings.char_render_mode + 1) % 2;
                    save_settings();
                    render_main_menu();
                } else if (menu_selection == 5) {
                    current_settings.no_color_mode = !current_settings.no_color_mode;
                    save_settings();
                    render_main_menu();
                } else if (menu_selection == 6) {
                    current_settings.save_target = (current_settings.save_target + 1) % 2;
                    save_settings();
                    render_main_menu();
                }
            }
        }
        else if (game_state == STATE_PLAYING) {
            if (current_settings.timer_mode == 1) {
                frame_counter++;
                if (frame_counter >= 60) {
                    frame_counter = 0;
                    if (time_left > 0) {
                        time_left--;
                    } else {
                        game_state = STATE_GAMEOVER;
                        ppu_write_str(0x224A, "TIME UP! GAME OVER");
                    }
                }
            }
            
            {
                int8_t dx = 0, dy = 0;
                if ((pad & 0x08) && !(last_pad & 0x08)) dy = -1;
                if ((pad & 0x04) && !(last_pad & 0x04)) dy = 1;
                if ((pad & 0x02) && !(last_pad & 0x02)) dx = -1;
                if ((pad & 0x01) && !(last_pad & 0x01)) dx = 1;
                
                if (dx != 0 || dy != 0) {
                    int8_t nx = p_x + dx;
                    int8_t ny = p_y + dy;
                    uint16_t start_addr = 0x2080 + ((32 - maze_w) / 2);
                    
                    if (nx >= 0 && nx < maze_w && ny >= 0 && ny < maze_h) {
                        if (maze[ny][nx] != '#') {
                            uint16_t old_addr = start_addr + (p_y * 32) + p_x;
                            ppu_put_tile(old_addr, (current_settings.maze_render_mode == 0) ? TILE_PATH : ('.' - 0x20));
                            maze[p_y][p_x] = '.';
                            
                            p_x = nx; p_y = ny;
                            
                            {
                                uint16_t new_addr = start_addr + (p_y * 32) + p_x;
                                if (current_settings.char_render_mode == 0) {
                                    ppu_put_tile(new_addr, (current_settings.character_type == 0) ? TILE_LIZARD_GFX : TILE_SNAKE_GFX);
                                } else {
                                    char player_sym = (current_settings.character_type == 0) ? 'L' : 'S';
                                    ppu_put_tile(new_addr, player_sym - 0x20);
                                }
                                maze[p_y][p_x] = (current_settings.character_type == 0) ? 'L' : 'S';
                            }
                            
                            if (p_x == e_x && p_y == e_y) {
                                current_round++;
                                generate_maze_world();
                                render_game_screen();
                            }
                        }
                    }
                }
            }
            
            if ((pad & 0x10) && !(last_pad & 0x10)) {
                game_state = STATE_PAUSE;
                ppu_write_str(0x21C2, "[PAUSED: A-RES B-MAIN MENU]");
            }
        }
        else if (game_state == STATE_PAUSE) {
            if ((pad & 0x80) && !(last_pad & 0x80)) {
                render_game_screen();
                game_state = STATE_PLAYING;
            }
            if ((pad & 0x40) && !(last_pad & 0x40)) {
                game_state = STATE_MAIN_MENU;
                render_main_menu();
            }
        }
        
        last_pad = pad;
    }
}
