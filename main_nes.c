#include <stdint.h>
#include <string.h>
#include <conio.h>

#define PPU_STATUS (*(volatile uint8_t*)0x2002)
#define JOYPAD1    (*(volatile uint8_t*)0x4016)

#define STATE_MAIN_MENU  0
#define STATE_CHAR_MENU  1
#define STATE_PLAYING    2
#define STATE_PAUSE      3
#define STATE_GAMEOVER   4

uint8_t game_state = STATE_MAIN_MENU;
uint8_t menu_selection = 0;
uint8_t char_menu_selection = 0;

typedef struct {
    uint8_t difficulty;       
    uint8_t character_type;   
    uint8_t timer_mode;       
    uint16_t custom_time;     
    uint8_t custom_size;      
    uint8_t color_theme;      // 0: Classic Color, 1: Emerald, 2: Sunset, 3: Monochrome (B&W)
    uint8_t save_target;      
} GameSettings;

const GameSettings IN_CART_DEFAULTS = {
    1,   // Medium Difficulty
    0,   // Lizard ('L')
    1,   // Timed Mode ON
    60,  // 60 Seconds
    1,   // Medium Size
    0,   // Classic Color Theme
    1    // In-Cartridge Storage
};

GameSettings current_settings;

uint16_t time_left = 60;
uint16_t max_time = 60;
uint8_t frame_counter = 0;
uint8_t current_round = 1;

#define MAX_MAZE_W 31
#define MAX_MAZE_H 25
uint8_t maze_w = 21;
uint8_t maze_h = 19;
char maze[MAX_MAZE_H][MAX_MAZE_W];

uint8_t p_x = 1, p_y = 1;
uint8_t e_x = 19, e_y = 17;

static uint8_t stack_x[256];
static uint8_t stack_y[256];

static uint16_t lcg_seed = 1986;
uint16_t next_rand(void) {
    lcg_seed = (lcg_seed * 25173UL + 13849UL + current_round) % 65535UL;
    return lcg_seed;
}

void save_settings(void) {
    volatile uint8_t* sram_magic = (volatile uint8_t*)0x6000;
    volatile uint8_t* sram_data = (volatile uint8_t*)0x6001;

    if (current_settings.save_target == 0) {
        sram_magic[0] = 'N';
        sram_magic[1] = 'E';
        memcpy((void*)sram_data, &current_settings, sizeof(GameSettings));
    } else {
        sram_magic[0] = 0x00;
        sram_magic[1] = 0x00;
    }
}

void load_settings(void) {
    volatile uint8_t* sram_magic = (volatile uint8_t*)0x6000;
    volatile uint8_t* sram_data = (volatile uint8_t*)0x6001;

    if (sram_magic[0] == 'N' && sram_magic[1] == 'E') {
        memcpy(&current_settings, (void*)sram_data, sizeof(GameSettings));
    } else {
        memcpy(&current_settings, &IN_CART_DEFAULTS, sizeof(GameSettings));
    }
}

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

void generate_maze_world(void) {
    int16_t r, c, i;
    
    if (current_settings.custom_size == 0) {       
        maze_w = 15; maze_h = 13; 
    } else if (current_settings.custom_size == 1) { 
        maze_w = 21; maze_h = 19; 
    } else {                                      
        maze_w = 25; maze_h = 21; 
    }

    if (current_settings.timer_mode == 0) {
        max_time = 0;
    } else {
        max_time = current_settings.custom_time;
    }

    time_left = max_time;

    for (r = 0; r < maze_h; r++) {
        for (c = 0; c < maze_w; c++) {
            maze[r][c] = '#';
        }
    }

    p_x = 1; p_y = 1;
    maze[p_y][p_x] = '.';

    {
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
        maze[e_y][e_x] = 'X'; 
    }
}

void render_main_menu(void) {
    clrscr();
    gotoxy(8, 1);  cputs("KGAMER NES MAZE");
    
    gotoxy(2, 3);  cputs(menu_selection == 0 ? "* START GAME" : "  START GAME");
    gotoxy(2, 5);  cputs(menu_selection == 1 ? "* MAZE SIZE:" : "  MAZE SIZE:");
    gotoxy(2, 7);  cputs(menu_selection == 2 ? "* TIMER:"      : "  TIMER:");
    gotoxy(2, 9);  cputs(menu_selection == 3 ? "* TIME VAL:"  : "  TIME VAL:");
    gotoxy(2, 11); cputs(menu_selection == 4 ? "* CHARACTER"  : "  CHARACTER");
    gotoxy(2, 13); cputs(menu_selection == 5 ? "* COLOR THEME": "  COLOR THEME");
    gotoxy(2, 15); cputs(menu_selection == 6 ? "* SAVE:"      : "  SAVE:");
    
    gotoxy(18, 5);  cputs(current_settings.custom_size == 0 ? "SMALL" : (current_settings.custom_size == 1 ? "MED  " : "LARGE"));
    gotoxy(18, 7);  cputs(current_settings.timer_mode == 0 ? "OFF" : "ON ");
    
    gotoxy(18, 9); 
    if(current_settings.timer_mode == 0) {
        cputs("N/A ");
    } else {
        cprintf("%uS  ", current_settings.custom_time);
    }
    
    gotoxy(18, 11); cputs(current_settings.character_type == 0 ? "LIZARD" : "SNAKE ");
    
    // Renk Teması Seçenekleri
    if(current_settings.color_theme == 0) cputs("  CLASSIC");
    else if(current_settings.color_theme == 1) cputs("  EMERALD");
    else if(current_settings.color_theme == 2) cputs("  SUNSET ");
    else cputs("  B&W    ");
    
    gotoxy(18, 13); // Kaydırıldı
    // Doğru satır hizalaması için tekrar konumlandırma:
    gotoxy(18, 13);
    // Aslında 13. satır color theme, 15. satır save target oldu:
    // Düzeltme:
    // 5: Size, 7: Timer, 9: TimeVal, 11: Char, 13: Color, 15: Save
    // Üstteki cputs satırlarını buna göre ayarlıyoruz:
    
    // Yeniden düzenlenmiş menü metin konumu:
    // 3: Start, 5: Size, 7: Timer, 9: TimeVal, 11: Char, 13: Color, 15: Save
    // Değerleri doğru yere basalım:
    
    gotoxy(18, 13);
    if(current_settings.color_theme == 0) cputs("CLASSIC");
    else if(current_settings.color_theme == 1) cputs("EMERALD");
    else if(current_settings.color_theme == 2) cputs("SUNSET ");
    else cputs("B&W    ");

    gotoxy(18, 15); cputs(current_settings.save_target == 0 ? "SRAM" : "ROM ");
    
    gotoxy(3, 21); cputs("UP/DOWN: NAVIGATE");
    gotoxy(3, 22); cputs("LEFT/RIGHT: CHANGE");
    gotoxy(3, 23); cputs("A BUTTON: SELECT");
}

void render_char_menu(void) {
    clrscr();
    gotoxy(7, 2);  cputs("CHARACTER CONFIG");
    gotoxy(3, 6);  cputs(char_menu_selection == 0 ? "* PLAYER:" : "  PLAYER:");
    gotoxy(3, 10); cputs(char_menu_selection == 1 ? "* BACK"    : "  BACK");
    
    gotoxy(15, 6); cputs(current_settings.character_type == 0 ? "LIZARD" : "SNAKE ");
    
    gotoxy(3, 22); cputs("UP/DOWN L/R: CHANGE");
    gotoxy(3, 23); cputs("A BUTTON: CONFIRM");
}

void render_game_screen(void) {
    uint8_t r, c;
    uint8_t offset_x = (32 - maze_w) / 2;
    uint8_t offset_y = 2; 
    
    clrscr();
    gotoxy(2, 23); cputs("RD:");
    gotoxy(5, 23); cprintf("%u", current_round);

    for (r = 0; r < maze_h; r++) {
        for (c = 0; c < maze_w; c++) {
            gotoxy(offset_x + c, offset_y + r);
            cputc(maze[r][c]);
        }
    }
    
    if (current_settings.timer_mode == 1) {
        gotoxy(20, 23); cputs("TIME:");
        gotoxy(25, 23); cprintf("%u  ", time_left);
    } else {
        gotoxy(20, 23); cputs("TIMELESS");
    }
}

void update_hud_timer(void) {
    if (current_settings.timer_mode == 1) {
        gotoxy(25, 23);
        cprintf("%u  ", time_left);
    }
}

void main(void) {
    uint8_t pad, last_pad = 0;
    
    load_settings();
    render_main_menu();
    
    while (1) {
        while (!(PPU_STATUS & 0x80)); 
        
        pad = read_controller();
        
        if (game_state == STATE_MAIN_MENU) {
            if ((pad & 0x08) && !(last_pad & 0x08)) {
                if (menu_selection > 0) menu_selection--;
                else menu_selection = 6;
                render_main_menu();
            }
            if ((pad & 0x04) && !(last_pad & 0x04)) {
                if (menu_selection < 6) menu_selection++;
                else menu_selection = 0;
                render_main_menu();
            }
            if (((pad & 0x01) && !(last_pad & 0x01)) || ((pad & 0x02) && !(last_pad & 0x02))) {
                if (menu_selection == 1) {
                    current_settings.custom_size = (current_settings.custom_size + 1) % 3;
                    save_settings();
                    render_main_menu();
                } else if (menu_selection == 2) {
                    current_settings.timer_mode = (current_settings.timer_mode + 1) % 2;
                    save_settings();
                    render_main_menu();
                } else if (menu_selection == 3) {
                    if (current_settings.timer_mode == 1) {
                        current_settings.custom_time += 15;
                        if (current_settings.custom_time > 120) current_settings.custom_time = 30;
                        save_settings();
                        render_main_menu();
                    }
                } else if (menu_selection == 5) {
                    current_settings.color_theme = (current_settings.color_theme + 1) % 4;
                    save_settings();
                    render_main_menu();
                } else if (menu_selection == 6) {
                    current_settings.save_target = (current_settings.save_target + 1) % 2;
                    save_settings();
                    render_main_menu();
                }
            }
            if ((pad & 0x80) && !(last_pad & 0x80)) {
                if (menu_selection == 0) {
                    current_round = 1;
                    generate_maze_world();
                    render_game_screen();
                    game_state = STATE_PLAYING;
                } else if (menu_selection == 4) {
                    char_menu_selection = 0;
                    render_char_menu();
                    game_state = STATE_CHAR_MENU;
                }
            }
        }
        else if (game_state == STATE_CHAR_MENU) {
            if ((pad & 0x08) && !(last_pad & 0x08)) {
                if (char_menu_selection > 0) char_menu_selection--;
                else char_menu_selection = 1;
                render_char_menu();
            }
            if ((pad & 0x04) && !(last_pad & 0x04)) {
                if (char_menu_selection < 1) char_menu_selection++;
                else char_menu_selection = 0;
                render_char_menu();
            }
            if (((pad & 0x01) && !(last_pad & 0x01)) || ((pad & 0x02) && !(last_pad & 0x02)) || ((pad & 0x80) && !(last_pad & 0x80))) {
                if (char_menu_selection == 0) {
                    current_settings.character_type = (current_settings.character_type + 1) % 2;
                    save_settings();
                    render_char_menu();
                } else if (char_menu_selection == 1) {
                    render_main_menu();
                    game_state = STATE_MAIN_MENU;
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
                        update_hud_timer();
                    } else {
                        game_state = STATE_GAMEOVER;
                        gotoxy(11, 23);
                        cputs("GAME OVER");
                    }
                }
            }
            
            {
                int8_t dx = 0, dy = 0;
                if ((pad & 0x08) && !(last_pad & 0x08)) dy = -1;
                if ((pad & 0x04) && !(last_pad & 0x04)) dy = 1;
                if ((pad & 0x02) && !(last_pad & 0x02)) dx = -1;
                if ((pad & 0x01) && !(last_pad & 0x01)) dx = 1;
                
                if (dx == 0 && dy == 0) {
                    if (pad & 0x08) dy = -1;
                    else if (pad & 0x04) dy = 1;
                    else if (pad & 0x02) dx = -1;
                    else if (pad & 0x01) dx = 1;
                }
                
                if (dx != 0 || dy != 0) {
                    int8_t nx = p_x + dx;
                    int8_t ny = p_y + dy;
                    uint8_t offset_x = (32 - maze_w) / 2;
                    uint8_t offset_y = 2;
                    
                    if (nx >= 0 && nx < maze_w && ny >= 0 && ny < maze_h) {
                        if (maze[ny][nx] != '#') {
                            gotoxy(offset_x + p_x, offset_y + p_y);
                            cputc('.');
                            maze[p_y][p_x] = '.';
                            
                            p_x = nx; p_y = ny;
                            
                            {
                                char player_sm = (current_settings.character_type == 0) ? 'L' : 'S';
                                gotoxy(offset_x + p_x, offset_y + p_y);
                                cputc(player_sm);
                                maze[p_y][p_x] = player_sm;
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
                gotoxy(12, 23);
                cputs("PAUSED");
            }
        }
        else if (game_state == STATE_PAUSE) {
            if ((pad & 0x80) && !(last_pad & 0x80)) {
                render_game_screen();
                game_state = STATE_PLAYING;
            }
            if ((pad & 0x40) && !(last_pad & 0x40)) {
                render_main_menu();
                game_state = STATE_MAIN_MENU;
            }
        }
        
        last_pad = pad;
    }
}
