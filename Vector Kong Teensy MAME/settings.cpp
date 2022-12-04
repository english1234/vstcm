/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to read and write settings to SD card

*/

#include <SD.h>
#include "settings.h"
#include "drawing.h"

char gMsg[50];                        // Optional additional information to show on menu

static DataChunk_t Chunk[NUMBER_OF_TEST_PATTERNS][MAX_PTS];
static int nb_points[NUMBER_OF_TEST_PATTERNS];
//
// Settings menu
//
int sel_setting;  // Currently selected setting

params_t v_setting[NB_SETTINGS] = {
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
  { "SERIAL_WAIT_TIME", "Test pattern delay", SERIAL_WAIT_TIME, 0, 255 }
};
//
// Menu choices on splash screen
//
int sel_splash;  // Currently selected menu item on splash screen


extern long fps;

extern "C" void read_vstcm_config();

void read_vstcm_config() {
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
      for (i = 1; i < NB_SETTINGS; i++) {
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
          if (!memcmp(param_name, v_setting[j].ini_label, pos_pn)) {
            /* Serial.print(param_name);
              Serial.print(" ");
              Serial.print(pos_pn);
              Serial.print(" characters long, AKA ");
              Serial.print(v_setting[j].ini_label);
              Serial.print(" ");
              Serial.print(sizeof v_setting[j].ini_label);
              Serial.print(" characters long, changed from ");
              Serial.print(v_setting[j].pval); */
            v_setting[j].pval = atoi(param_value);
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
  //  write_vstcm_config();
  }

  sel_setting = 0;  // Start at beginning of parameter list
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