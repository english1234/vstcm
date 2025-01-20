/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to set up pins on Teensy and handle SPI to DACs

*/

#ifndef _spi_fct_h_
#define _spi_fct_h_

//#define BUFFERED                          // If defined, uses buffer on DACs (according to the datasheet it should not be buffered since our Vref is Vdd)

// Teensy SS pins connected to DACs
const int CS_R_G_X_Y =  6;                  // Shared CS for Red, Green, x & y DACs IC4/IC5
const int CS_B       = 22;                  // Blue CS on IC3

// SPI0
#define SDI          11                     // MOSI on SPI0 IC3/IC4
#define SCK          13                     // SCK on SPI0 IC3/IC4

// SPI1
#define SDI1         26                     // MOSI for SPI1 IC5
#define MISO1        39                     // MISO for SPI1 - remap to not interfere with buttons (not the physical connection)
#define SCK1         27                     // SCK for SPI1 IC5
#define CS1          38                     // CS for SPI1 - remap to not interfere with buttons (not the physical connection)

// IMXRT_LPSPIx_S Registers
// Some additional SPI register bits to possibly work with
// as part of the Low-Power Serial Peripheral Interface (LPSPI)
// The Teensy 4.1 supports up to 4 SPI buses: SPI0, SPI1, SPI2, and SPI3
// These are the low-level memory-mapped hardware registers of the i.MX RT1062
// microcontroller that control the respective SPI peripherals:
// IMXRT_LPSPI4_S is the hardware register set for the SPI0 bus.
// IMXRT_LPSPI3_S is the hardware register set for the SPI1 bus
// Modifying the TCR register (IMXRT_LPSPIx_S.TCR) allows you to set the frame size, 
// transmit/receive mask, and other advanced parameters.
// Changing the CFGR1 register (IMXRT_LPSPIx_S.CFGR1) allows control over clock polarity,
// phase, and FIFO behaviour.
#define LPSPI_SR_WCF    ((uint32_t)(1<<8))  // Received Word complete flag
#define LPSPI_SR_FCF    ((uint32_t)(1<<9))  // Frame complete flag
#define LPSPI_SR_TCF    ((uint32_t)(1<<10)) // Transfer complete flag
#define LPSPI_SR_MBF    ((uint32_t)(1<<24)) // Module busy flag
#define LPSPI_TCR_RXMSK ((uint32_t)(1<<19)) // Receive Data Mask (when 1 no data received to FIFO)

const uint32_t CLOCKSPEED = 60000000;       // This is 3x the max clock in the datasheet but seems to work!!

void SPI_init();
void SPI_flush();

#endif