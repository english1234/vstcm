/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to read and write settings to SD card

*/

#include <SD.h>
#include "settings.h"
#include "drawing.h"

static DataChunk_t Chunk[NUMBER_OF_TEST_PATTERNS][MAX_PTS];
static int nb_points[NUMBER_OF_TEST_PATTERNS];
int opt_select;    // Currently selected setting

params_t v_config[NB_PARAMS] = {
  {"TEST_PATTERN",     "RGB test patterns",                0,                0,         4},
  {"OFF_SHIFT",        "Beam transit speed",               OFF_SHIFT,        0,        50},
  {"OFF_DWELL0",       "Beam settling delay",              OFF_DWELL0,       0,        50},
  {"OFF_DWELL1",       "Wait before beam transit",         OFF_DWELL1,       0,        50},
  {"OFF_DWELL2",       "Wait after beam transit",          OFF_DWELL2,       0,        50},
  {"NORMAL_SHIFT",     "Drawing Speed",                     NORMAL_SHIFT,     1,       255},
  {"FLIP_X",           "Flip X axis",                      FLIP_X,           0,         1},
  {"FLIP_Y",           "Flip Y axis",                      FLIP_Y,           0,         1},
  {"SWAP_XY",          "Swap XY",                          SWAP_XY,          0,         1},
  {"SHOW DT",          "Show DT",                          SHOW_DT,          0,         1},
  {"PINCUSHION",       "Pincushion adjustment",            PINCUSHION,       0,         1},
  {"IR_RECEIVE_PIN",   "IR receive pin",                   IR_RECEIVE_PIN,   0,        54},
  {"AUDIO_PIN",        "Audio pin",                        AUDIO_PIN,        0,        54},
  {"NORMAL1",          "Normal text brightness",           NORMAL1,          0,       255},
  {"BRIGHTER",         "Highlighted text brightness",      BRIGHTER,         0,       255},
  {"SERIAL_WAIT_TIME", "Test pattern delay",               SERIAL_WAIT_TIME, 0,       255}
};

extern long fps;

void read_vstcm_config()
{
  int i, j;
  const int chipSelect = BUILTIN_SDCARD;
  char buf;
  char param_name[20];
  char param_value[20];
  uint8_t pos_pn, pos_pv;

  // see if the SD card is present and can be initialised:
  if (!SD.begin(chipSelect))
  {
    //Serial.println("Card failed, or not present");
    // don't do anything more:
    return;
  }
  // else
  //  Serial.println("Card initialised.");

  // open the vstcm.ini file on the sd card
  File dataFile = SD.open("vstcm.ini", FILE_READ);

  if (dataFile)
  {
    while (dataFile.available())
    {
      for (i = 1; i < NB_PARAMS; i++)
      {
        pos_pn = 0;

        memset(param_name, 0, sizeof param_name);

        uint32_t read_start_time = millis();

        while (1)   // read the parameter name until an equals sign is encountered
        {

          // provide code for a timeout in case there's a problem reading the file

          if (millis() - read_start_time > 2000u)
          {
            //Serial.println("SD card read timeout");
            break;
          }

          buf = dataFile.read();

          if (buf == 0x3D)      // stop reading if it's an equals sign
            break;
          else if (buf != 0x0A && buf != 0x0d)      // ignore carriage return
          {
            param_name[pos_pn] = buf;
            pos_pn ++;
          }
        }

        pos_pv = 0;

        memset(param_value, 0, sizeof param_value);

        while (1)   // read the parameter value until a semicolon is encountered
        {

          // provide code for a timeout in case there's a problem reading the file
          if (millis() - read_start_time > 2000u)
          {
            //Serial.println("SD card read timeout");
            break;
          }

          buf = dataFile.read();

          if (buf == 0x3B)      // stop reading if it's a semicolon
            break;
          else if (buf != 0x0A && buf != 0x0d)      // ignore carriage return
          {
            param_value[pos_pv] = buf;
            pos_pv ++;
          }
        }

        // Find the setting in the predefined array and update the value with the one stored on SD

        bool bChanged = false;

        for (j = 0; j < NB_PARAMS; j++)
        {
          if (!memcmp(param_name, v_config[j].ini_label, pos_pn))
          {
            /* Serial.print(param_name);
              Serial.print(" ");
              Serial.print(pos_pn);
              Serial.print(" characters long, AKA ");
              Serial.print(v_config[j].ini_label);
              Serial.print(" ");
              Serial.print(sizeof v_config[j].ini_label);
              Serial.print(" characters long, changed from ");
              Serial.print(v_config[j].pval); */
            v_config[j].pval = atoi(param_value);
            //  Serial.print(" to ");
            //  Serial.println(v_config[j].pval);
            bChanged = true;
            break;
          }
        }

        if (bChanged == false)
        {
          //    Serial.print(param_name);
          //    Serial.println(" not found");
        }
      } // end of for i loop

      break;
    }

    // close the file:
    dataFile.close();
  }
  else
  {
    // if the file didn't open, print an error:
    //  Serial.println("Error opening file for reading");

    // If the vstcm.ini file doesn't exist, then write the default one
    write_vstcm_config();
  }

  opt_select = 0;     // Start at beginning of parameter list
}

void write_vstcm_config()
{
  int i;
  char buf[20];

  // Write the settings file to the SD card with currently selected values
  // Format of each line is <PARAMETER NAME>=<PARAMETER VALUE>; followed by a newline

  File dataFile = SD.open("vstcm.ini", O_RDWR);

  if (dataFile)
  {
    for (i = 0; i < NB_PARAMS; i++)
    {
      //  Serial.print("Writing ");
      //  Serial.print(v_config[i].ini_label);

      dataFile.write(v_config[i].ini_label);
      dataFile.write("=");
      memset(buf, 0, sizeof buf);
      ltoa(v_config[i].pval, buf, 10);

      //   Serial.print(" with value ");
      //   Serial.print(v_config[i].pval);
      //   Serial.print(" AKA ");
      //   Serial.println(buf);

      dataFile.write(buf);
      dataFile.write(";");
      dataFile.write(0x0d);
      dataFile.write(0x0a);
    }

    // close the file:
    dataFile.close();
  }
  else
  {
    // if the file didn't open, print an error:
    //  Serial.println("Error opening file for writing");
  }
}

void show_vstcm_config_screen()
{
  int i;
  char buf1[25] = "";

  if (v_config[0].pval != 0)      // show test pattern instead of settings
    draw_test_pattern(0);
  else
  {
    draw_string("v.st Colour Mod v3.0", 950, 3800, 10, v_config[14].pval);
    draw_test_pattern(1);
    
    // Show parameters on screen

    const int x = 300;
    int y = 2800;
    int intensity;
    const int line_size = 140;
    const int char_size = 5;
    const int x_offset = 3000;

    for (i = 0; i < NB_PARAMS; i++)
    {
      if (i == opt_select)      // Highlight currently selected parameter
        intensity = v_config[14].pval;
      else
        intensity = v_config[13].pval;

      draw_string(v_config[i].param, x, y, char_size, intensity);
      itoa(v_config[i].pval, buf1, 10);
      draw_string(buf1, x + x_offset, y, char_size, intensity);
      y -= line_size;
    }

    draw_string("PRESS CENTRE BUTTON / OK TO SAVE SETTINGS", 550, 400, 5, v_config[13].pval);

    draw_string("FPS:", 3000, 150, 6, v_config[13].pval);
    draw_string(itoa(fps, buf1, 10), 3400, 150, 6, v_config[13].pval);
  }
}

void make_test_pattern()
{
  // Prepare buffer of test pattern data as a speed optimisation

  int offset, i, j;

  offset = 0;   // Draw Asteroids style test pattern in Red, Green or Blue

  nb_points[offset] = 0;
  int intensity = 150;

  moveto(offset, 4095, 4095, 0, 0, 0);
  moveto(offset, 4095, 0, intensity, intensity, intensity);
  moveto(offset, 0, 0, intensity, intensity, intensity);
  moveto(offset, 0, 4095, intensity, intensity, intensity);
  moveto(offset, 4095, 4095, intensity, intensity, intensity);

  moveto(offset, 0, 0, 0, 0, 0);
  moveto(offset, 3071, 4095, intensity, intensity, intensity);
  moveto(offset, 4095, 2731, intensity, intensity, intensity);
  moveto(offset, 2048, 0, intensity, intensity, intensity);
  moveto(offset, 0, 2731, intensity, intensity, intensity);
  moveto(offset, 1024, 4095, intensity, intensity, intensity);
  moveto(offset, 4095, 0, intensity, intensity, intensity);

  moveto(offset, 0, 4095, 0, 0, 0);
  moveto(offset, 3071, 0, intensity, intensity, intensity);
  moveto(offset, 4095, 1365, intensity, intensity, intensity);
  moveto(offset, 2048, 4095, intensity, intensity, intensity);
  moveto(offset, 0, 1365, intensity, intensity, intensity);
  moveto(offset, 1024, 0, intensity, intensity, intensity);
  moveto(offset, 4095, 4095, intensity, intensity, intensity);
  moveto(offset, 4095, 4095, 0, 0, 0);

  // Prepare buffer for fixed part of settings screen

  offset = 1;
  nb_points[offset] = 0;

  // cross
  moveto(offset, 4095, 4095, 0, 0, 0);
  moveto(offset, 4095 - 512, 4095, 128, 128, 128);
  moveto(offset, 4095 - 512, 4095 - 512, 128, 128, 128);
  moveto(offset, 4095, 4095 - 512, 128, 128, 128);
  moveto(offset, 4095, 4095, 128, 128, 128);

  moveto(offset, 0, 4095, 0, 0, 0);
  moveto(offset, 512, 4095, 128, 128, 128);
  moveto(offset, 0, 4095 - 512, 128, 128, 128);
  moveto(offset, 512, 4095 - 512, 128, 128, 128);
  moveto(offset, 0, 4095, 128, 128, 128);

  // Square
  moveto(offset, 0, 0, 0, 0, 0);
  moveto(offset, 512, 0, 128, 128, 128);
  moveto(offset, 512, 512, 128, 128, 128);
  moveto(offset, 0, 512, 128, 128, 128);
  moveto(offset, 0, 0, 128, 128, 128);

  // triangle
  moveto(offset, 4095, 0, 0, 0, 0);
  moveto(offset, 4095 - 512, 0, 128, 128, 128);
  moveto(offset, 4095 - 0, 512, 128, 128, 128);
  moveto(offset, 4095, 0, 128, 128, 128);

  // RGB gradiant scale

  const uint16_t height = 3072;
  const int mult = 5;

  for (i = 0, j = 31 ; j <= 255 ; i += 8, j += 32)  //Start at 31 to end up at full intensity?
  {
    moveto(offset, 1100, height + i * mult, 0, 0, 0);
    moveto(offset, 1500, height + i * mult, j, 0, 0);     // Red
    moveto(offset, 1600, height + i * mult, 0, 0, 0);
    moveto(offset, 2000, height + i * mult, 0, j, 0);     // Green
    moveto(offset, 2100, height + i * mult, 0, 0, 0);
    moveto(offset, 2500, height + i * mult, 0, 0, j);     // Blue
    moveto(offset, 2600, height + i * mult, 0, 0, 0);
    moveto(offset, 3000, height + i * mult, j, j, j);     // all 3 colours combined
  }
}

void moveto(int offset, int x, int y, int red, int green, int blue)
{
  // Store coordinates of vectors and colour info in a buffer

  DataChunk_t *localChunk = &Chunk[offset][nb_points[offset]];

  localChunk->x = x;
  localChunk->y = y;
  localChunk->red = red;
  localChunk->green = green;
  localChunk->blue = blue;

  nb_points[offset] ++;
}

void draw_test_pattern(int offset)
{
  int i, red = 0, green = 0, blue = 0;

  if (offset == 0)      // Determine what colour to draw the test pattern
  {
    if (v_config[0].pval == 1)
      red = 140;
    else if (v_config[0].pval == 2)
      green = 140;
    else if (v_config[0].pval == 3)
      blue = 140;
    else if (v_config[0].pval == 4)
    {
      red = 140;
      green = 140;
      blue = 140;
    }

    for (i = 0; i < nb_points[offset]; i++)
    {
      if (Chunk[offset][i].red == 0)
        draw_moveto(Chunk[offset][i].x, Chunk[offset][i].y);
      //   draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, 0, 0, 0);
      else
        // draw_to_xyrgb(Chunk[i].x, Chunk[i].y, Chunk[i].red, Chunk[i].green, Chunk[i].blue);
        draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, red, green, blue);
    }
  }
  else if (offset == 1)
  {
    for (i = 0; i < nb_points[offset]; i++)
      draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, Chunk[offset][i].red, Chunk[offset][i].green, Chunk[offset][i].blue);
  }
}
