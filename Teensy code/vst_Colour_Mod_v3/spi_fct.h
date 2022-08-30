/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to set up pins on Teensy and handle SPI to DACs

*/

#ifndef _spi_fct_h_
#define _spi_fct_h_

#define SDI                 11        // MOSI on SPI0
#define SCK                 13        // SCK on SPI0
//#define BUFFERED                      // If defined, uses buffer on DACs (According to the datasheet it should not be buffered since our Vref is Vdd)

#define SDI1                26       // MOSI for SPI1
#define MISO1               39        //MISO for SPI1 - remap to not interfere with buttons
#define SCK1                27        //SCK for SPI1
#define CS1                 38        //CS for SPI1 - remap to not interfere with buttons

//Some additional SPI register bits to possibly work with
#define LPSPI_SR_WCF ((uint32_t)(1<<8)) //received Word complete flag
#define LPSPI_SR_FCF ((uint32_t)(1<<9)) //Frame complete flag
#define LPSPI_SR_TCF ((uint32_t)(1<<10)) //Transfer complete flag
#define LPSPI_SR_MBF ((uint32_t)(1<<24)) //Module busy flag
#define LPSPI_TCR_RXMSK ((uint32_t)(1<<19)) //Receive Data Mask (when 1 no data received to FIFO)

// Teensy SS pins connected to DACs
const int CS_R_G_X_Y       =  6;       // Shared CS for red,green,x,y dacs
const int CS_B = 22;       // Blue CS
const uint32_t CLOCKSPEED = 60000000; //This is 3x the max clock in the datasheet but seems to work!!

void SPI_init();
void SPI_flush();

#endif
