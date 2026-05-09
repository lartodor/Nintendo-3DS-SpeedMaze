#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAZE_WIDTH 49
#define MAZE_HEIGHT 27
#define MAX_LEVELS 10

char maze[MAZE_HEIGHT][MAZE_WIDTH];
int player_x = 1;
int player_y = 1;

// --- SAVE DATA VARIABLES ---
double best_sectors[MAX_LEVELS];
double best_full = 999.99; // Default "no record" time
double current_sectors[MAX_LEVELS];

// --- AUDIO STRUCT ---
typedef struct {
    u32* buffer;
    u32 dataSize;
    u32 numSamples;
    ndspWaveBuf waveBuf;
} SoundEffect;

SoundEffect step_sfx;
SoundEffect win_sfx;

// Color Customization Arrays
int bg_colors[] = {41, 42, 43, 44, 45, 46, 47}; 
int fg_colors[] = {31, 32, 33, 34, 35, 36, 37}; 
const char* color_names[] = {"RED", "GRN", "YLW", "BLU", "MAG", "CYN", "WHT"};

int wall_color = 6;  
int player_color = 1;

int dx[] = {0, 0, -2, 2};
int dy[] = {-2, 2, 0, 0};

// --- SAVE/LOAD FUNCTIONS ---
void load_save() {
    // Looks for the save file on the SD card
    FILE* f = fopen("sdmc:/10maze_save.dat", "rb");
    if (f) {
        fread(best_sectors, sizeof(double), MAX_LEVELS, f);
        fread(&best_full, sizeof(double), 1, f);
        fclose(f);
    } else {
        // If no save exists, set all bests to 999.99 seconds
        for (int i = 0; i < MAX_LEVELS; i++) {
            best_sectors[i] = 999.99;
        }
        best_full = 999.99;
    }
}

void save_game() {
    // Writes the new best times to the SD card
    FILE* f = fopen("sdmc:/10maze_save.dat", "wb");
    if (f) {
        fwrite(best_sectors, sizeof(double), MAX_LEVELS, f);
        fwrite(&best_full, sizeof(double), 1, f);
        fclose(f);
    }
}

// --- CUSTOM WAV LOADER ---
bool load_wav(const char* path, SoundEffect* sfx, int channel) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    u16 channels;
    u32 sampleRate;
    u16 bitsPerSample;

    fseek(f, 22, SEEK_SET);
    fread(&channels, 2, 1, f);
    fread(&sampleRate, 4, 1, f);
    fseek(f, 34, SEEK_SET);
    fread(&bitsPerSample, 2, 1, f);

    fseek(f, 12, SEEK_SET);
    char chunkId[4];
    u32 chunkSize;
    
    sfx->buffer = NULL; 
    
    while (fread(chunkId, 1, 4, f) == 4) {
        fread(&chunkSize, 4, 1, f);
        if (memcmp(chunkId, "data", 4) == 0) {
            sfx->dataSize = chunkSize;
            sfx->buffer = (u32*)linearAlloc(sfx->dataSize);
            if (sfx->buffer) {
                fread(sfx->buffer, 1, sfx->dataSize, f);
            }
            break;
        }
        fseek(f, chunkSize, SEEK_CUR); 
    }
    fclose(f);

    if (!sfx->buffer) return false;

    sfx->numSamples = sfx->dataSize / (channels * (bitsPerSample / 8));

    u16 ndspFormat;
    if (bitsPerSample == 8) {
        ndspFormat = (channels == 2) ? NDSP_FORMAT_STEREO_PCM8 : NDSP_FORMAT_MONO_PCM8;
    } else {
        ndspFormat = (channels == 2) ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16;
    }

    ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(channel, ndspFormat);
    ndspChnSetRate(channel, sampleRate);

    memset(&sfx->waveBuf, 0, sizeof(ndspWaveBuf));
    sfx->waveBuf.data_vaddr = sfx->buffer;
    sfx->waveBuf.nsamples = sfx->numSamples;
    sfx->waveBuf.looping = false;
    sfx->waveBuf.status = NDSP_WBUF_FREE;

    DSP_FlushDataCache(sfx->buffer, sfx->dataSize);
    return true;
}

// --- AUDIO SETUP FUNCTION ---
void init_sound() {
    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    load_wav("romfs:/step.wav", &step_sfx, 0);
    load_wav("romfs:/win.wav", &win_sfx, 1);
}

// --- MAZE FUNCTIONS ---
void carve_maze(int x, int y) {
    maze[y][x] = ' ';
    
    int dirs[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; i++) {
        int r = rand() % 4;
        int temp = dirs[i];
        dirs[i] = dirs[r];
        dirs[r] = temp;
    }
    
    for (int i = 0; i < 4; i++) {
        int nx = x + dx[dirs[i]];
        int ny = y + dy[dirs[i]];
        
        if (nx > 0 && nx < MAZE_WIDTH - 1 && ny > 0 && ny < MAZE_HEIGHT - 1 && maze[ny][nx] == '#') {
            maze[y + dy[dirs[i]] / 2][x + dx[dirs[i]] / 2] = ' '; 
            carve_maze(nx, ny);
        }
    }
}

void draw_maze(PrintConsole* topScreen) {
    consoleSelect(topScreen);
    printf("\x1b[0;0H"); 

    for (int y = 0; y < MAZE_HEIGHT; y++) {
        for (int x = 0; x < MAZE_WIDTH; x++) {
            if (maze[y][x] == '#') {
                printf("\x1b[%dm \x1b[0m", bg_colors[wall_color]);
            } else if (maze[y][x] == 'E') {
                printf("\x1b[45mE\x1b[0m"); 
            } else {
                printf(" "); 
            }
        }
        printf("\n");
    }
    
    printf("\x1b[%d;%dH\x1b[%dm \x1b[0m", player_y + 1, player_x + 1, bg_colors[player_color]);
}

void generate_level(int level, PrintConsole* topScreen) {
    srand(1000 + level); 
    
    for (int y = 0; y < MAZE_HEIGHT; y++) {
        for (int x = 0; x < MAZE_WIDTH; x++) {
            maze[y][x] = '#';
        }
    }
    
    carve_maze(1, 1);
    maze[MAZE_HEIGHT - 2][MAZE_WIDTH - 2] = 'E'; 
    
    player_x = 1;
    player_y = 1;

    draw_maze(topScreen);
}

int main(int argc, char **argv) {
    gfxInitDefault();
    romfsInit(); 
    
    PrintConsole topScreen, bottomScreen;
    consoleInit(GFX_TOP, &topScreen);
    consoleInit(GFX_BOTTOM, &bottomScreen);

    init_sound();
    load_save(); // Load previous best times from SD Card!

    int current_level = 1;
    bool timer_started = false;
    bool game_won = false;
    u64 level_start_time = 0;

    int move_cooldown = 0;
    const int MOVEMENT_SPEED = 4;

    // Reset current run sectors
    for(int i=0; i<MAX_LEVELS; i++) current_sectors[i] = 0.0;

    generate_level(current_level, &topScreen);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START) break; 

        // --- PLAYER MOVEMENT ---
        if (!game_won) {
            if (move_cooldown > 0) {
                move_cooldown--;
            }

            if (move_cooldown == 0) {
                int next_x = player_x;
                int next_y = player_y;

                if (kHeld & (KEY_UP | KEY_CPAD_UP))         next_y--;
                else if (kHeld & (KEY_DOWN | KEY_CPAD_DOWN))   next_y++;
                else if (kHeld & (KEY_LEFT | KEY_CPAD_LEFT))   next_x--;
                else if (kHeld & (KEY_RIGHT | KEY_CPAD_RIGHT)) next_x++;

                if (next_x != player_x || next_y != player_y) {
                    // Start timer on first move of the sector
                    if (!timer_started) {
                        timer_started = true;
                        level_start_time = osGetTime();
                    }

                    if (maze[next_y][next_x] != '#') {
                        consoleSelect(&topScreen);
                        printf("\x1b[%d;%dH ", player_y + 1, player_x + 1);
                        
                        player_x = next_x;
                        player_y = next_y;
                        
                        printf("\x1b[%d;%dH\x1b[%dm \x1b[0m", player_y + 1, player_x + 1, bg_colors[player_color]);
                        move_cooldown = MOVEMENT_SPEED;

                        // Play step sound
                        if (step_sfx.buffer && (step_sfx.waveBuf.status == NDSP_WBUF_DONE || step_sfx.waveBuf.status == NDSP_WBUF_FREE)) {
                            ndspChnWaveBufAdd(0, &step_sfx.waveBuf);
                        }

                        // --- LEVEL COMPLETE LOGIC ---
                        if (maze[player_y][player_x] == 'E') {
                            
                            // Play win sound
                            if (win_sfx.buffer) {
                                ndspChnWaveBufClear(1); 
                                ndspChnWaveBufAdd(1, &win_sfx.waveBuf);
                            }

                            // Lock in sector time
                            double final_sector_time = (osGetTime() - level_start_time) / 1000.0;
                            current_sectors[current_level - 1] = final_sector_time;

                            current_level++;
                            move_cooldown = 15; 

                            if (current_level > MAX_LEVELS) {
                                // GAME BEATEN
                                game_won = true;
                                timer_started = false;

                                // Calculate total time and update records
                                double current_full = 0.0;
                                bool new_record = false;
                                
                                for (int i = 0; i < MAX_LEVELS; i++) {
                                    current_full += current_sectors[i];
                                    if (current_sectors[i] < best_sectors[i]) {
                                        best_sectors[i] = current_sectors[i];
                                        new_record = true;
                                    }
                                }
                                
                                if (current_full < best_full) {
                                    best_full = current_full;
                                    new_record = true;
                                }

                                if (new_record) save_game(); // Write to SD Card

                            } else {
                                // NEXT SECTOR
                                generate_level(current_level, &topScreen);
                                // Automatically start the timer for the next sector
                                level_start_time = osGetTime();
                            }
                        }
                    }
                }
            }
        }

        // --- BOTTOM SCREEN UI (SECTOR TRACKER) ---
        consoleSelect(&bottomScreen);
        printf("\x1b[0;0H"); 
        printf("========================================\n");
        printf("           10-MAZE SPEEDRUN             \n");
        printf("========================================\n\n");
        
        double running_total = 0.0;
        
        // Loop through all 10 levels to display them
        for (int i = 0; i < MAX_LEVELS; i++) {
            printf(" Sector %d: ", i + 1);

            if (i < current_level - 1) {
                // Completed Sectors
                double t = current_sectors[i];
                running_total += t;

                if (best_sectors[i] == 999.99 || t == best_sectors[i]) {
                    printf("\x1b[37m"); // White (Same / No Record)
                } else if (t < best_sectors[i]) {
                    printf("\x1b[32m"); // Green (Faster!)
                } else {
                    printf("\x1b[31m"); // Red (Slower)
                }
                printf("%5.2f s\x1b[0m\n", t);

            } else if (i == current_level - 1 && !game_won) {
                // Currently Active Sector (Live Time)
                double live_t = timer_started ? (osGetTime() - level_start_time) / 1000.0 : 0.0;
                running_total += live_t;

                if (best_sectors[i] == 999.99 || live_t == best_sectors[i]) {
                    printf("\x1b[37m"); // White
                } else if (live_t < best_sectors[i]) {
                    printf("\x1b[32m"); // Green
                } else {
                    printf("\x1b[31m"); // Red
                }
                printf("%5.2f s\x1b[0m  <--\n", live_t);
            } else {
                // Future Sectors
                printf("\x1b[37m--.-- s\x1b[0m\n");
            }
        }

        printf("\n----------------------------------------\n");
        printf(" RUN TOTAL : %.2f s\n", running_total);
        if (best_full == 999.99) {
            printf(" BEST TOTAL: --.-- s\n");
        } else {
            printf(" BEST TOTAL: \x1b[33m%.2f s\x1b[0m\n", best_full);
        }

        if (game_won) {
            printf("\n\x1b[32mRUN COMPLETE! Press START to exit.\x1b[0m\n");
        } else {
            printf("\n[ START = Exit ]\n");
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    if (step_sfx.buffer) linearFree(step_sfx.buffer);
    if (win_sfx.buffer) linearFree(win_sfx.buffer);

    ndspExit();
    romfsExit(); 
    gfxExit();
    return 0;
}