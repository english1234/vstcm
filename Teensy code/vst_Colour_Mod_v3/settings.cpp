/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to read and write settings to SD card and draw menus on screen

*/

#include "main.h"

#ifdef VSTCM
#include <SD.h>
#else
#pragma warning(disable : 4996)     // Get rid of annoying compiler warnings in VC++
#include <cstring>
#include <stdlib.h>
#endif

#include "settings.h"
#include "drawing.h"

char gMsg[50];                        // Optional additional information to show on menu

// Cached vectors for test patterns: may be better to generate these dynamically
// to avoid using memory
static DataChunk_t Chunk[NUMBER_OF_TEST_PATTERNS][MAX_PTS];
static int nb_points[NUMBER_OF_TEST_PATTERNS];
//
// Settings menu
//
int sel_setting;  // Currently selected menu choice

// ADD MENU CHOICES
//
// B&W vs COLOUR
// Amplifone / G05 etc.

params_t v_setting[2][18] = {
  {
      { "TEST_PATTERN", "RGB test patterns", 0, 0, 4 },
      { "OFF_SHIFT", "Beam transit speed", OFF_SHIFT, 0, 50 },
      { "OFF_DWELL0", "Beam settling delay", OFF_DWELL0, 0, 50 },
      { "OFF_DWELL1", "Wait before beam transit", OFF_DWELL1, 0, 50 },
      { "OFF_DWELL2", "Wait after beam transit", OFF_DWELL2, 0, 50 },
      { "NORMAL_SHIFT", "Drawing speed", NORMAL_SHIFT, 1, 255 },
      { "FLIP_X", "Flip X axis", FLIP_X, 0, 1 },
      { "FLIP_Y", "Flip Y axis", FLIP_Y, 0, 1 },
      { "SWAP_XY", "Swap XY", SWAP_XY, 0, 1 },
      { "SHOW DT", "Show DT", SHOW_DT, 0, 1 },
      { "PINCUSHION", "Pincushion adjustment", PINCUSHION, 0, 1 },
      { "IR_RECEIVE_PIN", "IR receive pin", IR_RECEIVE_PIN, 0, 54 },
      { "AUDIO_PIN", "Audio pin", AUDIO_PIN, 0, 54 },
      { "NORMAL1", "Normal text brightness", NORMAL1, 0, 255 },
      { "BRIGHTER", "Highlighted text brightness", BRIGHTER, 0, 255 },
      { "SERIAL_WAIT_TIME", "Test pattern delay", SERIAL_WAIT_TIME, 0, 255 },
      { "COLOUR_SWITCH", "Colour / Monochrome display", COLOUR_SWITCH, 0, 1 },
  },
  {
      { "BZONE", "Battlezone", 0, 0, 0 },
      { "tailgunner", "Tail Gunner", 0, 0, 0 },
      { "warrior", "Warrior", 0, 0, 0 },
      { "armorattack", "Armor Attack", 0, 0, 0 },
      { "boxingbugs", "Boxing Bugs", 0, 0, 0 },
      { "demon", "Demon", 0, 0, 0 },
      { "ripoff", "Rip Off", 0, 0, 0 },
      { "spacewars", "Space Wars", 0, 0, 0 },
      { "starcastle", "Star Castle", 0, 0, 0 },
      { "starhawk", "Star Hawk", 0, 0, 0 },
      { "speedfreak", "Speed Freak", 0, 0, 0 },
      { "solarquest", "Solar Quest", 0, 0, 0 },
      { "cosmicchasm", "Cosmic Chasm", 0, 0, 0 },
      { "waroftheworlds", "War of the Worlds", 0, 0, 0 },
      { "barrier", "Barrier", 0, 0, 0 },
      { "sundance", "Sundance", 0, 0, 0 },
      { "qb3", "QB3", 0, 0, 0 },
      { "SETTINGS", "Settings menu", 0, 0, 0 },
  }
};

extern long fps;
//#else
//long fps; // Don't really care about this on PC
// #endif

void read_vstcm_config() {
#ifdef VSTCM
  int i, j;
  const int chipSelect = BUILTIN_SDCARD;
  char buf;
  char param_name[20];
  char param_value[20];
  uint8_t pos_pn, pos_pv;

  // see if the SD card is present and can be initialised:
  if (!SD.begin(chipSelect)) {
    //Serial.println("Card failed, or not present");
    // don't do anything more:
    return;
  }
  // else
  //  Serial.println("Card initialised.");

  // open the vstcm.ini file on the sd card
  File dataFile = SD.open("vstcm.ini", FILE_READ);

  if (dataFile) {
    while (dataFile.available()) {
      for (i = 0; i < NB_SETTINGS; i++) {
        pos_pn = 0;

        memset(param_name, 0, sizeof param_name);

        uint32_t read_start_time = millis();

        while (1)  // read the parameter name until an equals sign is encountered
        {

          // provide code for a timeout in case there's a problem reading the file

          if (millis() - read_start_time > 2000u) {
            //Serial.println("SD card read timeout");
            break;
          }

          buf = dataFile.read();

          if (buf == 0x3D)  // stop reading if it's an equals sign
            break;
          else if (buf != 0x0A && buf != 0x0d)  // ignore carriage return
          {
            param_name[pos_pn] = buf;
            pos_pn++;
          }
        }

        pos_pv = 0;

        memset(param_value, 0, sizeof param_value);

        while (1)  // read the parameter value until a semicolon is encountered
        {

          // provide code for a timeout in case there's a problem reading the file
          if (millis() - read_start_time > 2000u) {
            //Serial.println("SD card read timeout");
            break;
          }

          buf = dataFile.read();

          if (buf == 0x3B)  // stop reading if it's a semicolon
            break;
          else if (buf != 0x0A && buf != 0x0d)  // ignore carriage return
          {
            param_value[pos_pv] = buf;
            pos_pv++;
          }
        }

        // Find the setting in the predefined array and update the value with the one stored on SD

        bool bChanged = false;

        for (j = 0; j < NB_SETTINGS; j++) {
          if (!memcmp(param_name, v_setting[SETTINGS_MENU][j].ini_label, pos_pn)) {
            /* Serial.print(param_name);
              Serial.print(" ");
              Serial.print(pos_pn);
              Serial.print(" characters long, AKA ");
              Serial.print(v_setting[j].ini_label);
              Serial.print(" ");
              Serial.print(sizeof v_setting[j].ini_label);
              Serial.print(" characters long, changed from ");
              Serial.print(v_setting[j].pval); */
            v_setting[SETTINGS_MENU][j].pval = atoi(param_value);
            //  Serial.print(" to ");
            //  Serial.println(v_setting[j].pval);
            bChanged = true;
            break;
          }
        }

        if (bChanged == false) {
          //    Serial.print(param_name);
          //    Serial.println(" not found");
        }
      }  // end of for i loop

      break;
    }

    // close the file:
    dataFile.close();
  } else {
    // if the file didn't open, print an error:
    //  Serial.println("Error opening file for reading");

    // If the vstcm.ini file doesn't exist, then write the default one
    write_vstcm_config();
  }

  sel_setting = 0;  // Start at beginning of parameter list
#endif
}

void write_vstcm_config() {
#ifdef VSTCM
  int i;
  char buf[20];

  // Write the settings file to the SD card with currently selected values
  // Format of each line is <PARAMETER NAME>=<PARAMETER VALUE>; followed by a newline

  File dataFile = SD.open("vstcm.ini", O_RDWR);

  if (dataFile) {
    for (i = 0; i < NB_SETTINGS; i++) {
      //  Serial.print("Writing ");
      //  Serial.print(v_setting[SETTINGS_MENU][i].ini_label);

      dataFile.write(v_setting[SETTINGS_MENU][i].ini_label);
      dataFile.write("=");
      memset(buf, 0, sizeof buf);
      ltoa(v_setting[SETTINGS_MENU][i].pval, buf, 10);

      //   Serial.print(" with value ");
      //   Serial.print(v_setting[SETTINGS_MENU][i].pval);
      //   Serial.print(" AKA ");
      //   Serial.println(buf);

      dataFile.write(buf);
      dataFile.write(";");
      dataFile.write(0x0d);
      dataFile.write(0x0a);
    }

    // close the file:
    dataFile.close();
  } else {
    // if the file didn't open, print an error:
    //  Serial.println("Error opening file for writing");
  }
#endif
}

void show_vstcm_menu_screen(int which) {
    int i, x, y, x_offset = 3000, intensity, line_size = 128, char_size = 5;
    list_t menu_list[2];
    char buf1[25] = "";

    menu_list[SETTINGS_MENU].nb_menu_items = NB_SETTINGS;
    menu_list[SETTINGS_MENU].choices = &v_setting[0][0];
    menu_list[SPLASH_MENU].nb_menu_items = NB_SPLASH_CHOICES;
    menu_list[SPLASH_MENU].choices = &v_setting[1][0];

    intensity = v_setting[SETTINGS_MENU][13].pval;

    if (which == SPLASH_MENU) { // Show splash screen
        static int logo_x = 1920;
        static int logo_y = 3500;
        static int logo_size = 1;
        static int logo_offset = 1;
        static int logo_brightness = 10;

        draw_string("VSTCM", logo_x, logo_y, logo_size, logo_brightness);

        // Animate the VSTCM logo: gets bigger and brighter then smaller and darker on each execution of loop()
        logo_size += logo_offset;
        logo_x -= (30 * logo_offset);
        logo_brightness += (4 * logo_offset);

        if (logo_size < 1 || logo_size > 25)
            logo_offset = -logo_offset;

        // Show menu choices on splash screen

         x = 1500;
         y = 3000;
         line_size = 128;
         char_size = 6;

        // Show an additional message if required
        if (strlen(gMsg) > 0)
            draw_string(gMsg, 200, 600, 6, intensity );

        draw_string("CHOOSE A GAME OR CONNECT MAME TO USB", 200, 400, 7, intensity );
        draw_string("Press DOWN on PCB to exit game", 700, 200, 6, intensity );
    }
    else if (which == SETTINGS_MENU) {    // Show settings screen
        if (v_setting[SETTINGS_MENU][0].pval != 0)  // show test pattern instead of settings
            draw_test_pattern(0);
        else {
            draw_string("v.st Colour Mod v3.0", 950, 3800, 10, v_setting[SETTINGS_MENU][14].pval);
            draw_test_pattern(1);

            x = 300;
            y = 3000;
            line_size = 140;
            char_size = 5;
            x_offset = 3000;

            draw_string("PRESS LEFT & RIGHT TO CHANGE VALUES", 800, 550, 5, intensity );
            draw_string("PRESS CENTRE BUTTON / OK TO SAVE SETTINGS", 550, 400, 5, intensity );
            draw_string("FPS:", 3000, 150, 6, intensity );
#ifdef VSTCM
            draw_string(itoa(fps, buf1, 10), 3400, 150, 6, intensity );
#else
            draw_string(_itoa(fps, buf1, 10), 3400, 150, 6, intensity );
#endif
        }
    }

    // Only if we're not showing a test pattern
    if (v_setting[SETTINGS_MENU][0].pval == 0) {
     // Display the list of menu choices
       for (i = 0; i < menu_list[which].nb_menu_items; i++) {
          if (i == sel_setting) {  // Highlight currently selected parameter
             intensity = v_setting[SETTINGS_MENU][14].pval;
             draw_string( menu_list[which].choices[i].param, x, y, char_size + 1, intensity );
          }
          else {     // Use standard intensity if not selected
             intensity = v_setting[SETTINGS_MENU][13].pval;
             draw_string( menu_list[which].choices[i].param, x, y, char_size, intensity );
          }

          // On the settings menu, show the value of the setting
          if (which == SETTINGS_MENU) {
#ifdef VSTCM
             itoa( v_setting[SETTINGS_MENU][i].pval, buf1, 10 );
#else
             _itoa( v_setting[SETTINGS_MENU][i].pval, buf1, 10 ); // POSIX deprecated blah blah blah
#endif
             draw_string( buf1, x + x_offset, y, char_size, intensity );
          }

          y -= line_size;
       }
    }
}

void make_test_pattern() {
  // Prepare buffer of test pattern data as a speed optimisation

  int offset, i, j;

  // Draw Asteroids style test pattern in Red, Green or Blue
  offset = 0;  
  nb_points[offset] = 0;
  const int intensity = 150;

  static const uint16_t positions[] = {
     4095, 4095, 0, 0, 0,
     4095, 0, intensity, intensity, intensity,
     0, 0, intensity, intensity, intensity,
     0, 4095, intensity, intensity, intensity,
     4095, 4095, intensity, intensity, intensity,
     0, 0, 0, 0, 0,
     3071, 4095, intensity, intensity, intensity,
     4095, 2731, intensity, intensity, intensity,
     2048, 0, intensity, intensity, intensity,
     0, 2731, intensity, intensity, intensity,
     1024, 4095, intensity, intensity, intensity,
     4095, 0, intensity, intensity, intensity,
     0, 4095, 0, 0, 0,
     3071, 0, intensity, intensity, intensity,
     4095, 1365, intensity, intensity, intensity,
     2048, 4095, intensity, intensity, intensity,
     0, 1365, intensity, intensity, intensity,
     1024, 0, intensity, intensity, intensity,
     4095, 4095, intensity, intensity, intensity,
     4095, 4095, 0, 0, 0 };
 
  for (i = 0; i < 100; i += 5)
      moveto(offset, positions[i], positions[i+1], positions[i+2], positions[i+3], positions[i+4]);
  
  // Prepare buffer for fixed part of settings screen
  offset = 1;
  nb_points[offset] = 0;
  const int intensity2 = 128;

  static const uint16_t positions2[] = {
      // cross
      4095, 4095, 0, 0, 0,
      3583, 4095, intensity2, intensity2, intensity2,
      3583, 3583, intensity2, intensity2, intensity2,
      4095, 3583, intensity2, intensity2, intensity2,
      4095, 4095, intensity2, intensity2, intensity2,
      0, 4095, 0, 0, 0,
      512, 4095, intensity2, intensity2, intensity2,
      0, 3583, intensity2, intensity2, intensity2,
      512, 3583, intensity2, intensity2, intensity2,
      0, 4095, intensity2, intensity2, intensity2,
      // Square
      0, 0, 0, 0, 0,
      512, 0, intensity2, intensity2, intensity2,
      512, 512, intensity2, intensity2, intensity2,
      0, 512, intensity2, intensity2, intensity2,
      0, 0, intensity2, intensity2, intensity2,
      // triangle
      4095, 0, 0, 0, 0,
      4095 - 512, 0, intensity2, intensity2, intensity2,
      4095 - 0, 512, intensity2, intensity2, intensity2,
      4095, 0, intensity2, intensity2, intensity2 };

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

  DataChunk_t *localChunk = &Chunk[offset][nb_points[offset]];

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
    if (v_setting[SETTINGS_MENU][0].pval == 1)
      red = 140;
    else if (v_setting[SETTINGS_MENU][0].pval == 2)
      green = 140;
    else if (v_setting[SETTINGS_MENU][0].pval == 3)
      blue = 140;
    else if (v_setting[SETTINGS_MENU][0].pval == 4) {
      red = 140;
      green = 140;
      blue = 140;
    }

    for (i = 0; i < nb_points[offset]; i++) {
      if (Chunk[offset][i].red == 0)
        draw_moveto(Chunk[offset][i].x, Chunk[offset][i].y);
      else
        draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, red, green, blue);
    }
  } else if (offset == 1) {
    for (i = 0; i < nb_points[offset]; i++)
      draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, Chunk[offset][i].red, Chunk[offset][i].green, Chunk[offset][i].blue);
  }
}