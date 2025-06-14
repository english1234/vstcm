/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to read and write settings to SD card and draw menus on screen

*/

#include "main.h"

#ifdef VSTCM
#include <SD.h>
#else
#pragma warning(disable : 4996)  // Get rid of annoying compiler warnings in VC++
#include <cstring>
#include <stdlib.h>
#endif

#include "buttons.h"
#include "settings.h"
#include "drawing.h"

char gMsg[50];  // Optional additional information to show on menu

// Global menu instances
Menu g_main_menu;
Menu g_settings_menu;
Menu g_games_menu;

Menu* g_current_menu = &g_main_menu; // Start with the main menu

// Cached vectors for test patterns: may be better to generate these dynamically
// to avoid using memory
static DataChunk_t Chunk[NUMBER_OF_TEST_PATTERNS][MAX_PTS];
static int nb_points[NUMBER_OF_TEST_PATTERNS];

// Currently selected menu choice (for compatibility with existing code)
int sel_setting = 0;

extern long fps;

void make_test_pattern() {
    // Prepare buffer of test pattern data as a speed optimisation

    int offset, i, j;

    // Draw Asteroids style test pattern in Red, Green or Blue
    offset = 0;
    nb_points[offset] = 0;

    for (i = 0; i < 100; i += 5)
        moveto(offset, positions[i], positions[i + 1], positions[i + 2], positions[i + 3], positions[i + 4]);

    // Prepare buffer for fixed part of settings screen
    offset = 1;
    nb_points[offset] = 0;

    for (i = 0; i < 95; i += 5)
        moveto(offset, positions2[i], positions2[i + 1], positions2[i + 2], positions2[i + 3], positions2[i + 4]);

    // RGB gradiant scale
    const int height = 3200;
    const int mult = 5;
    const int colors[] = { 0, 31, 63, 95, 127, 159, 191, 223, 255 };

    for (i = 0, j = 1; j <= 8; ++i, ++j) {
        int color = colors[j];
        int yOffset = height + (i << mult);

        moveto(offset, 1100, yOffset, 0, 0, 0);
        moveto(offset, 1500, yOffset, color, 0, 0);  // Red
        moveto(offset, 1600, yOffset, 0, 0, 0);
        moveto(offset, 2000, yOffset, 0, color, 0);  // Green
        moveto(offset, 2100, yOffset, 0, 0, 0);
        moveto(offset, 2500, yOffset, 0, 0, color);  // Blue
        moveto(offset, 2600, yOffset, 0, 0, 0);
        moveto(offset, 3000, yOffset, color, color, color);  // all 3 colours combined
    }
}

void moveto(int offset, int x, int y, int red, int green, int blue) {
    // Store coordinates of vectors and colour info in a buffer

    DataChunk_t* localChunk = &Chunk[offset][nb_points[offset]];

    localChunk->x = x;
    localChunk->y = y;
    localChunk->red = red;
    localChunk->green = green;
    localChunk->blue = blue;

    nb_points[offset]++;
}

void draw_test_pattern(int offset) {
    int i, red = 0, green = 0, blue = 0;

    if (offset == 0)  // Determine what colour to draw the test pattern
    {
        // Find test pattern setting in menu
        MenuItem* test_pattern_item = find_menu_item_by_ini_label(&g_settings_menu, "TEST_PATTERN");
        if (test_pattern_item) {
            if (test_pattern_item->value == 1)
                red = 140;
            else if (test_pattern_item->value == 2)
                green = 140;
            else if (test_pattern_item->value == 3)
                blue = 140;
            else if (test_pattern_item->value == 4) {
                red = 140;
                green = 140;
                blue = 140;
            }
        }

        for (i = 0; i < nb_points[offset]; i++) {
            if (Chunk[offset][i].red == 0)
                draw_moveto(Chunk[offset][i].x, Chunk[offset][i].y);
            else
                draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, red, green, blue);
        }
    }
    else if (offset == 1) {
        for (i = 0; i < nb_points[offset]; i++)
            draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, Chunk[offset][i].red, Chunk[offset][i].green, Chunk[offset][i].blue);
    }
}

// --- MenuItem Helper Functions ---
void menu_item_increment(MenuItem* item) {
    if (!item) return;
    if (item->type == MENU_ITEM_TYPE_NUMERIC && item->value < item->max_val)
        item->value++;

    if (item->type == MENU_ITEM_TYPE_BOOLEAN)
        item->value = !item->value;
}

void menu_item_decrement(MenuItem* item) {
    if (!item) return;
    if (item->type == MENU_ITEM_TYPE_NUMERIC && item->value > item->min_val)
        item->value--;

    if (item->type == MENU_ITEM_TYPE_BOOLEAN)
        item->value = !item->value;
}

void menu_item_execute_action(MenuItem* item, void* context) {
    if (!item) return;
    if (item->type == MENU_ITEM_TYPE_ACTION && item->action_func)
        item->action_func(item, context);
}

// --- Menu Helper Functions ---
void menu_init(Menu* menu, const char* title, Menu* parent) {
    if (!menu) return;
    strncpy(menu->title, title, sizeof(menu->title) - 1);
    menu->title[sizeof(menu->title) - 1] = '\0';
    menu->item_count = 0;
    menu->selected_item_index = 0;
    menu->display_offset = 0;
    menu->parent_menu = parent;
}

void menu_add_item(Menu* menu, MenuItem item) {
    if (!menu || menu->item_count >= MAX_MENU_ITEMS) return;
    menu->items[menu->item_count++] = item;
}

void menu_navigate_down(Menu* menu) {
    if (!menu || menu->item_count == 0) return;
    menu->selected_item_index = (menu->selected_item_index + 1) % menu->item_count;

    // Update display offset for scrolling
    const int MAX_VISIBLE_ITEMS_ON_SCREEN = 18;
    if (menu->selected_item_index >= menu->display_offset + MAX_VISIBLE_ITEMS_ON_SCREEN) {
        menu->display_offset = menu->selected_item_index - MAX_VISIBLE_ITEMS_ON_SCREEN + 1;
    }
    if (menu->selected_item_index < menu->display_offset) {
        menu->display_offset = menu->selected_item_index;
    }
}

void menu_navigate_up(Menu* menu) {
    if (!menu || menu->item_count == 0) return;
    menu->selected_item_index = (menu->selected_item_index - 1 + menu->item_count) % menu->item_count;

    // Update display offset for scrolling
    const int MAX_VISIBLE_ITEMS_ON_SCREEN = 18;
    if (menu->selected_item_index < menu->display_offset) {
        menu->display_offset = menu->selected_item_index;
    }
    if (menu->selected_item_index >= menu->display_offset + MAX_VISIBLE_ITEMS_ON_SCREEN) {
        menu->display_offset = menu->selected_item_index - MAX_VISIBLE_ITEMS_ON_SCREEN + 1;
    }
}

MenuItem* menu_get_selected_item(Menu* menu) {
    if (!menu || menu->selected_item_index < 0 || menu->selected_item_index >= menu->item_count)
        return NULL;

    return &menu->items[menu->selected_item_index];
}

// --- Action Functions ---
void action_go_to_parent_menu(MenuItem* item, void* context) {
    (void)item; // Unused parameter
    Menu* current = (Menu*)context;
    if (current && current->parent_menu) {
        g_current_menu = current->parent_menu;
        g_current_menu->selected_item_index = 0;
        g_current_menu->display_offset = 0;
    }
}

void action_launch_game(MenuItem* item, void* context) {
    (void)context; // Unused parameter
    if (!item) return;

    // Launch game based on emulator type
    extern int vecsim(char*);
    extern bool cinemu_setup(const char*);
    extern int show_vstcm_settings;

    show_vstcm_settings = NO_MENU; // Hide menu while game runs

    if (item->emulator == emu_vecsim) {
        vecsim(item->ini_label);
    }
    else if (item->emulator == emu_cinemu) {
        cinemu_setup(item->ini_label);
    }
    else if (item->emulator == emu_6809) {
        // Add 6809 emulator call here when available
        vecsim(item->ini_label); // Fallback to vecsim for now
    }
    else if (item->emulator == emu_68000) {
        // Add 68000 emulator call here when available
        vecsim(item->ini_label); // Fallback to vecsim for now
    }

    // Return to games menu after game ends
    g_current_menu = &g_games_menu;
    show_vstcm_settings = SPLASH_MENU;
}

void action_save_settings(MenuItem* item, void* context) {
    write_vstcm_config();
    // Could add visual feedback here
}

// --- Menu Setup Functions ---
MenuItem create_menu_item(const char* label, MenuItemType type, int32_t value,
    int32_t min_val, int32_t max_val, const char* ini_label,
    uint8_t emulator, void (*action_func)(MenuItem*, void*),
    Menu* sub_menu) {
    MenuItem item;
    strncpy(item.label, label, sizeof(item.label) - 1);
    item.label[sizeof(item.label) - 1] = '\0';
    item.type = type;
    item.value = value;
    item.min_val = min_val;
    item.max_val = max_val;
    if (ini_label) {
        strncpy(item.ini_label, ini_label, sizeof(item.ini_label) - 1);
        item.ini_label[sizeof(item.ini_label) - 1] = '\0';
    }
    else {
        item.ini_label[0] = '\0';
    }
    item.emulator = emulator;
    item.action_func = action_func;
    item.sub_menu = sub_menu;
    return item;
}

void setup_all_menus() {
    // Initialize menus
    menu_init(&g_main_menu, "Main Menu", NULL);
    menu_init(&g_settings_menu, "Settings", &g_main_menu);
    menu_init(&g_games_menu, "Select Game", &g_main_menu);

    // Main Menu Items
    menu_add_item(&g_main_menu, create_menu_item("Settings", MENU_ITEM_TYPE_SUB_MENU, 0, 0, 0, NULL, 0, NULL, &g_settings_menu));
    menu_add_item(&g_main_menu, create_menu_item("Select Game", MENU_ITEM_TYPE_SUB_MENU, 0, 0, 0, NULL, 0, NULL, &g_games_menu));

    // Settings Menu Items
    menu_add_item(&g_settings_menu, create_menu_item("RGB test patterns", MENU_ITEM_TYPE_NUMERIC, 0, 0, 4, "TEST_PATTERN", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Beam transit speed", MENU_ITEM_TYPE_NUMERIC, OFF_SHIFT, 0, 50, "OFF_SHIFT", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Beam settling delay", MENU_ITEM_TYPE_NUMERIC, OFF_DWELL0, 0, 50, "OFF_DWELL0", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Wait before beam transit", MENU_ITEM_TYPE_NUMERIC, OFF_DWELL1, 0, 50, "OFF_DWELL1", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Wait after beam transit", MENU_ITEM_TYPE_NUMERIC, OFF_DWELL2, 0, 50, "OFF_DWELL2", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Drawing speed", MENU_ITEM_TYPE_NUMERIC, NORMAL_SHIFT, 1, 255, "NORMAL_SHIFT", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Flip X axis", MENU_ITEM_TYPE_BOOLEAN, FLIP_X, 0, 1, "FLIP_X", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Flip Y axis", MENU_ITEM_TYPE_BOOLEAN, FLIP_Y, 0, 1, "FLIP_Y", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Swap XY", MENU_ITEM_TYPE_BOOLEAN, SWAP_XY, 0, 1, "SWAP_XY", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Show DT", MENU_ITEM_TYPE_BOOLEAN, SHOW_DT, 0, 1, "SHOW_DT", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Pincushion adjustment", MENU_ITEM_TYPE_BOOLEAN, PINCUSHION, 0, 1, "PINCUSHION", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("IR receive pin", MENU_ITEM_TYPE_NUMERIC, IR_RECEIVE_PIN, 0, 54, "IR_RECEIVE_PIN", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Audio pin", MENU_ITEM_TYPE_NUMERIC, AUDIO_PIN, 0, 54, "AUDIO_PIN", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Normal text brightness", MENU_ITEM_TYPE_NUMERIC, NORMAL1, 0, 255, "NORMAL1", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Highlighted text brightness", MENU_ITEM_TYPE_NUMERIC, BRIGHTER, 0, 255, "BRIGHTER", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Test pattern delay", MENU_ITEM_TYPE_NUMERIC, SERIAL_WAIT_TIME, 0, 255, "SERIAL_WAIT_TIME", 0, NULL, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Colour / Monochrome display", MENU_ITEM_TYPE_BOOLEAN, COLOUR_SWITCH, 0, 1, "COLOUR_SWITCH", 0, NULL, NULL));

    // Add save and back options
    menu_add_item(&g_settings_menu, create_menu_item("Save Settings", MENU_ITEM_TYPE_ACTION, 0, 0, 0, NULL, 0, action_save_settings, NULL));
    menu_add_item(&g_settings_menu, create_menu_item("Back", MENU_ITEM_TYPE_ACTION, 0, 0, 0, NULL, 0, action_go_to_parent_menu, NULL));

    // Games Menu Items
    menu_add_item(&g_games_menu, create_menu_item("Asteroids", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "asteroids", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Asteroids Deluxe", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "deluxe", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Black Widow", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "blackwidow", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Battlezone", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "battlezone", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Gravitar", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "gravitar", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Lunar Lander", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "lunar", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Major Havoc", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "majorhavoc", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Quantum", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "quantum", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Red Baron", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "redbaron", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Space Duel", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "spaceduel", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Star Wars", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "starwars", emu_6809, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Tempest", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "tempest", emu_vecsim, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("The Empire Strikes Back", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "empire", emu_6809, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Armor Attack", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "armorattack", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Barrier", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "barrier", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Boxing Bugs", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "boxingbugs", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Cosmic Chasm", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "cosmicchasm", emu_68000, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Demon", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "demon", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("QB3", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "qb3", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Rip Off", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "ripoff", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Solar Quest", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "solarquest", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Space Wars", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "spacewars", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Speed Freak", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "speedfreak", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Star Castle", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "starcastle", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Star Hawk", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "starhawk", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Sundance", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "sundance", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Tail Gunner", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "tailgunner", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("War of the Worlds", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "waroftheworlds", emu_cinemu, action_launch_game, NULL));
    menu_add_item(&g_games_menu, create_menu_item("Warrior", MENU_ITEM_TYPE_ACTION, 0, 0, 0, "warrior", emu_cinemu, action_launch_game, NULL));

    // Add back option
    menu_add_item(&g_games_menu, create_menu_item("Back", MENU_ITEM_TYPE_ACTION, 0, 0, 0, NULL, 0, action_go_to_parent_menu, NULL));
}

void handle_menu_input(int input_key) {
    if (!g_current_menu) return;

    MenuItem* selected = menu_get_selected_item(g_current_menu);

    switch (input_key) {
    case INPUT_KEY_UP:
        menu_navigate_up(g_current_menu);
        break;
    case INPUT_KEY_DOWN:
        menu_navigate_down(g_current_menu);
        break;
    case INPUT_KEY_LEFT:
        if (selected) menu_item_decrement(selected);
        break;
    case INPUT_KEY_RIGHT:
        if (selected) menu_item_increment(selected);
        break;
    case INPUT_KEY_SELECT: // OK button
        if (selected) {
            if (selected->type == MENU_ITEM_TYPE_ACTION) {
                menu_item_execute_action(selected, g_current_menu);
            }
            else if (selected->type == MENU_ITEM_TYPE_SUB_MENU && selected->sub_menu) {
                g_current_menu = selected->sub_menu;
                g_current_menu->selected_item_index = 0;
                g_current_menu->display_offset = 0;
            }
        }
        break;
    case INPUT_KEY_BACK:
        if (g_current_menu->parent_menu) {
            g_current_menu = g_current_menu->parent_menu;
        }
        break;
    }

 if (selected && (selected->type == MENU_ITEM_TYPE_NUMERIC || selected->type == MENU_ITEM_TYPE_BOOLEAN)) 
    update_goto_xy_settings();  // Apply the setting changes immediately
}

// --- Configuration Loading/Saving ---
MenuItem* find_menu_item_by_ini_label(Menu* menu, const char* ini_label) {
    if (!menu || !ini_label) return NULL;
    for (int i = 0; i < menu->item_count; ++i) {
        if (strcmp(menu->items[i].ini_label, ini_label) == 0) {
            return &menu->items[i];
        }
    }
    return NULL;
}

void read_vstcm_config() {
#ifdef VSTCM
    const int chipSelect = BUILTIN_SDCARD;
    if (!SD.begin(chipSelect)) {
        return;
    }

    File dataFile = SD.open("vstcm.ini", FILE_READ);
    if (dataFile) {
        char line_buffer[100];
        int buf_idx = 0;
        char c;

        while (dataFile.available()) {
            c = dataFile.read();
            if (c == '\n' || c == '\r') {
                line_buffer[buf_idx] = '\0';
                if (buf_idx > 0) {
                    char param_name[20];
                    char param_value_str[20];
                    int i = 0, j = 0;

                    // Parse param_name
                    while (line_buffer[i] != '=' && line_buffer[i] != '\0' && i < sizeof(param_name) - 1) {
                        param_name[i] = line_buffer[i];
                        i++;
                    }
                    param_name[i] = '\0';

                    if (line_buffer[i] == '=') {
                        i++;
                        // Parse param_value_str
                        while (line_buffer[i] != ';' && line_buffer[i] != '\0' && j < sizeof(param_value_str) - 1) {
                            param_value_str[j] = line_buffer[i];
                            i++;
                            j++;
                        }
                        param_value_str[j] = '\0';

                        MenuItem* item_to_update = find_menu_item_by_ini_label(&g_settings_menu, param_name);
                        if (item_to_update) {
                            item_to_update->value = atoi(param_value_str);
                        }
                    }
                }
                buf_idx = 0;
            }
            else {
                if (buf_idx < sizeof(line_buffer) - 1) {
                    line_buffer[buf_idx++] = c;
                }
            }
        }
        dataFile.close();
    }
    else {
        // If the file didn't open, write a default one
        write_vstcm_config();
    }
#endif
}

void write_vstcm_config() {
#ifdef VSTCM
    const int chipSelect = BUILTIN_SDCARD;
    if (!SD.begin(chipSelect)) {
        return;
    }

    File dataFile = SD.open("vstcm.ini", FILE_WRITE);
    if (dataFile) {
        for (int i = 0; i < g_settings_menu.item_count; ++i) {
            MenuItem* item = &g_settings_menu.items[i];
            // Only save items that have an ini_label and are configurable
            if (strlen(item->ini_label) > 0 &&
                (item->type == MENU_ITEM_TYPE_NUMERIC || item->type == MENU_ITEM_TYPE_BOOLEAN)) {
                char buf_val[20];
                snprintf(buf_val, sizeof(buf_val), "%ld", item->value);

                dataFile.write((const uint8_t*)item->ini_label, strlen(item->ini_label));
                dataFile.write('=');
                dataFile.write((const uint8_t*)buf_val, strlen(buf_val));
                dataFile.write(';');
                dataFile.write('\r');
                dataFile.write('\n');
            }
        }
        dataFile.close();
    }
#endif
}

static int x = 1500;
static int y = 3000;
static int line_size = 128;


void draw_menu_on_screen(Menu* menu) {
    if (!menu) return;

    // Get brightness settings from menu items
    MenuItem* normal_brightness_item = find_menu_item_by_ini_label(&g_settings_menu, "NORMAL1");
    MenuItem* highlight_brightness_item = find_menu_item_by_ini_label(&g_settings_menu, "BRIGHTER");

    int normal_intensity = normal_brightness_item ? normal_brightness_item->value : 150;
    int highlight_intensity = highlight_brightness_item ? highlight_brightness_item->value : 230;
    int current_item_intensity;

    // Draw menu title
   // draw_string(menu->title, 950, 3800, 10, highlight_intensity);

    int y_pos = 3000;
    int line_height = 140;
    int char_size = 5;
    int x_pos = 300;
    int value_x_offset = 3000;

    const int MAX_VISIBLE_ITEMS_ON_SCREEN = 18;

    static int logo_x = 1920;
    static int logo_y = 3600;
    static int logo_size = 1;
    static int logo_offset = 1;
    static int logo_brightness = 10;

    draw_string("VSTCM", logo_x, logo_y, logo_size, logo_brightness);

    // Animate the VSTCM logo
    logo_size += logo_offset;
    logo_x -= (30 * logo_offset);
    logo_brightness += (4 * logo_offset);

    if (logo_size < 1 || logo_size > 25)
        logo_offset = -logo_offset;

    // Show menu choices on splash screen
    char_size = 6;

    // Show additional message if needed
    if (strlen(gMsg) > 0)
        draw_string(gMsg, 200, 600, 6, intensity);

    // Handle scrolling logic
    if (menu->item_count > MAX_VISIBLE_ITEMS_ON_SCREEN) {
        if (menu->selected_item_index >= menu->display_offset + MAX_VISIBLE_ITEMS_ON_SCREEN) {
            menu->display_offset = menu->selected_item_index - MAX_VISIBLE_ITEMS_ON_SCREEN + 1;
        }
        if (menu->selected_item_index < menu->display_offset) {
            menu->display_offset = menu->selected_item_index;
        }
        if (menu->display_offset < 0) menu->display_offset = 0;
        if (menu->display_offset > menu->item_count - MAX_VISIBLE_ITEMS_ON_SCREEN) {
            menu->display_offset = menu->item_count - MAX_VISIBLE_ITEMS_ON_SCREEN;
        }
    }

    // Draw visible menu items
    for (int i = 0; i < MAX_VISIBLE_ITEMS_ON_SCREEN; ++i) {
        int item_idx_to_draw = menu->display_offset + i;

        if (item_idx_to_draw >= menu->item_count) break;

        MenuItem* current_item = &menu->items[item_idx_to_draw];
        char value_str[20];

        // Determine intensity based on selection
        if (item_idx_to_draw == menu->selected_item_index) {
            current_item_intensity = highlight_intensity;
            draw_string(current_item->label, x_pos, y_pos, char_size + 1, current_item_intensity);
        }
        else {
            current_item_intensity = normal_intensity;
            draw_string(current_item->label, x_pos, y_pos, char_size, current_item_intensity);
        }

        // Draw value for NUMERIC or BOOLEAN types
        if (menu == &g_settings_menu) {
            if (current_item->type == MENU_ITEM_TYPE_NUMERIC) {
                snprintf(value_str, sizeof(value_str), "%ld", current_item->value);
                draw_string(value_str, x_pos + value_x_offset, y_pos, char_size, current_item_intensity);
            }
            else if (current_item->type == MENU_ITEM_TYPE_BOOLEAN) {
                draw_string(current_item->value ? "ON" : "OFF", x_pos + value_x_offset, y_pos, char_size, current_item_intensity);
            }
        }
        y_pos -= line_height;
    }

    // Draw additional information based on menu type
    if (menu == &g_settings_menu) {
        draw_string("PRESS LEFT & RIGHT TO CHANGE VALUES", 800, 400, 5, normal_intensity);
     //   draw_string("PRESS CENTRE BUTTON / OK TO SAVE SETTINGS", 550, 400, 5, normal_intensity);

        // Draw FPS if available
        char fps_str[20];
        snprintf(fps_str, sizeof(fps_str), "FPS: %ld", fps);
        draw_string(fps_str, 3000, 150, 6, normal_intensity);

        // Draw test pattern if enabled
        MenuItem* test_pattern_item = find_menu_item_by_ini_label(menu, "TEST_PATTERN");
        if (test_pattern_item && test_pattern_item->value != 0) {
            draw_test_pattern(0);
        }
        else
            draw_test_pattern(1);
    }
    else  {
        draw_string("CHOOSE A GAME OR CONNECT MAME TO USB", 200, 400, 7, intensity);
        draw_string("Press DOWN on PCB to exit game", 700, 200, 6, intensity);
    }
}