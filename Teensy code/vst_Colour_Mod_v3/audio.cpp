/*
    SD WAV mixer via PWM
    by Nathan Ramanathan (Github: https://github.com/nathanRamaNoodles)

    Compatibility: Arduino Uno/nano/mini, Teensy 3.x
*/

#include <SPI.h>
#include "SD.h"  //faster SD library

char dataFilename[] = "roms/Battlezone/explode1.wav";  //wav files

//#define FS_music 32E3  //Sample Rate at 32,000 Hz
#define FS_music 48E3  //Sample Rate at 48,000 Hz
#define buffSize 1000  //Buffer Size

static void timerInterrupt();
static IntervalTimer sampleTimer;
const int SD_ChipSelectPin = BUILTIN_SDCARD;
const int audioPin = 5;

File dataFile;

uint8_t* buffPointer;       //pointer
uint8_t buf[buffSize];      //buffer
uint8_t backBuf[buffSize];  //backup buffer for efficiency
uint8_t lastValue;          //most recent value from SD card played to Speaker
uint32_t filePosition;      //Current file position
bool buffTrigger;           //Triggered when Buffer is ready.(not a good idea, due to lag)
bool backBuffFlag;          //Tells when Speaker has finished playing buffer
bool finished;              //song is finished
bool toggle;                //alternate between backup and main buffer
int location;

void audio_setup() {
  backBuffFlag = true;  //Tells when Speaker has finished playing buffer
  buffTrigger = false;  //Triggered when Buffer is ready.(not a good idea, due to lag)
  finished = false;     //song is finished
  toggle = false;       //alternate between backup and main buffer
  location = 0;
  filePosition = 0;  //Current file position
  lastValue = 0;     //most recent value from SD card played to Speaker

  // Serial.begin(115200);
  //  while (!Serial);

  /*  if (!SD.begin(SD_ChipSelectPin)) {
    Serial.println("SD fail");
    return;
  }*/
  dataFile = SD.open(dataFilename, FILE_READ);

  if (!dataFile)
    Serial.println("File not opened");

  dataFile.seek(44);  //seek to beginning of main wav file data
  filePosition = 44;

  if (sampleTimer.begin(timerInterrupt, 1000000.0 / FS_music) == false)
    Serial.println("All hardware timers are in use");

  analogWriteFrequency(audioPin, FS_music);  // setup our timerInterrupts
}

void audio_loop() {
  if (!backBuffFlag) {
    toggle = !toggle;
    if (toggle) {
      if (!finished) {
        if (dataFile.read(backBuf, sizeof(backBuf)) == 0)
          finished = true;
      }
    } else {
      if (!finished) {
        if (dataFile.read(buf, sizeof(buf)) == 0)
          finished = true;
      }
    }
    backBuffFlag = true;
    filePosition += buffSize;
    if (!finished && (dataFile.size() < filePosition)) {
      finished = true;
      //     Serial.println("Done 1");
    }
  } else {
    if (!buffTrigger) {
      if (!finished) {
        if (toggle)
          buffPointer = backBuf;
        else
          buffPointer = buf;
      }

      toggle = !toggle;
      buffTrigger = true;
      backBuffFlag = false;
    }
  }
}

static void timerInterrupt() {
  if (location < buffSize) {
    //Shift 2 bits to the right. This is faster than dividing by Four.
    //add 127 to make it a bit louder
    int sample = ((finished ? lastValue : lastValue = buffPointer[location]) >> 2) + 127;

    location++;  //increment our pointer's location

    //OUTPUT to our Audio pin.

    if (sample != 127)
      analogWrite(audioPin, sample);
  } else {
    buffTrigger = false;
    location = 0;
  }
}