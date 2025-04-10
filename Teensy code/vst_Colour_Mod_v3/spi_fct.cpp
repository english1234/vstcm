/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to set up pins on Teensy and handle SPI to DACs

*/

#include "main.h"

#ifdef VSTCM
#include <SPI.h>
#include "spi_fct.h"

volatile int Spiflag, Spi1flag;  //Keeps track of an active SPI transaction in progress

#endif

#ifdef PT8211_SOUND

#include "imxrt.h"  // Required for direct register access

void setup_pt8211() {
    // Enable clock for SAI1
    CCM_CCGR5 |= CCM_CCGR5_SAI1(CCM_CCGR_ON);

    // Configure the MCLK output for SAI1
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_03 = 3;  // Set pin to SAI1_MCLK
    IOMUXC_SW_PAD_CTL_PAD_GPIO_B0_03 = 0x10;  // Slow slew rate, low drive strength

    // Configure TX BCLK, TX SYNC, and TX DATA pins
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_02 = 3;  // SAI1_TX_BCLK
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_01 = 3;  // SAI1_TX_SYNC
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_00 = 3;  // SAI1_TX_DATA0

    // Disable SAI1 transmitter before configuration
    IMXRT_SAI1.TCSR &= ~I2S_TCSR_TE;

    // Configure TX settings
    IMXRT_SAI1.TCR2 = I2S_TCR2_SYNC(0) | I2S_TCR2_BCP;
    IMXRT_SAI1.TCR3 = I2S_TCR3_TCE | (1 << 16);  // Enable transmit channel 0
    IMXRT_SAI1.TCR4 = I2S_TCR4_FRSZ(1) | I2S_TCR4_SYWD(15) | I2S_TCR4_MF | I2S_TCR4_FSE;
    IMXRT_SAI1.TCR5 = I2S_TCR5_WNW(15) | I2S_TCR5_W0W(15) | I2S_TCR5_FBT(15);

    // Set FIFO watermark to 1 to reduce latency
    IMXRT_SAI1.TCR1 = 1;

    // Enable SAI1 transmitter
    IMXRT_SAI1.TCSR = I2S_TCSR_FRDE | I2S_TCSR_TE | I2S_TCSR_BCE;
}
#endif

void SPI_init() {
#ifdef VSTCM
  uint32_t mytcr;  // Keeps track of what the TCR register should be put back to after 16 bit mode
                   // - bit of a hack but reads and writes are a bit funny for this register (FIFOs?)

  // Pins reserved for future version of VSTCM
  // Uncommenting this stops code running, requires debugging! 
 /* pinMode(CS_IC4, OUTPUT);
  digitalWriteFast(CS_IC4, HIGH);
  delayNanoseconds(100);
  pinMode(SDI_IC4, OUTPUT);
  digitalWriteFast(SDI_IC4, HIGH);
  delayNanoseconds(100);
  pinMode(SCK_IC4, OUTPUT);
  digitalWriteFast(SCK_IC4, HIGH);
  delayNanoseconds(100);*/

  // Set chip select pins going to IC4/IC5 DACs to output
  pinMode(CS_R_G_X_Y, OUTPUT);
  digitalWriteFast(CS_R_G_X_Y, HIGH);
  delayNanoseconds(100);

  // Set chip select pins going to IC3 DAC to output
  pinMode(CS_B, OUTPUT);
  digitalWriteFast(CS_B, HIGH);
  delayNanoseconds(100);

  pinMode(SDI, OUTPUT);  // Set up clock and data output to DACs
  pinMode(SCK, OUTPUT);
  pinMode(SDI1, OUTPUT);  // Set up clock and data output to DACs
  pinMode(SCK1, OUTPUT);
  delay(1);  // https://www.pjrc.com/better-spi-bus-design-in-3-steps/

  //NOTE:  SPI uses LPSPI4 and SPI1 uses LPSPI3
  Spiflag = 0;
  Spi1flag = 0;
  SPI.setCS(10);
  SPI.begin();

  //Hopefully this will properly map the SPI1 pins
  SPI1.setMISO(MISO1);
  SPI1.setCS(CS1);
  SPI1.setMOSI(SDI1);
  SPI1.setSCK(SCK1);

  SPI1.begin();

  //Some posts seem to indicate that doing a begin and end like this will help conflicts with other things on the Teensy??
  SPI.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));  //Doing this begin and end here should make it so we don't have to do it each time
  SPI.endTransaction();
  SPI1.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));  //Doing this begin and end here should make it so we don't have to do it each time
  SPI1.endTransaction();
  SPI.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));
  SPI1.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));

  // Purpose of Modifying the TCR
  // 16-Bit Data Transfers: By setting the frame size to 16 bits, the SPI hardware is configured to handle data in 16-bit chunks
  // rather than the default 8 bits.
  // Performance Optimization: Disabling the receiver (RXMSK) reduces overhead for applications where received data is not needed,
  // making the SPI transactions faster and more efficient.

  // Configure Frame Size for LPSPI3 and LPSPI4
  mytcr = IMXRT_LPSPI4_S.TCR;  // Save the transfer control register for LPSPI4

  // (mytcr & 0xfffff000) clears the lower 12 bits of the register, which include the frame size and other settings.
  // LPSPI_TCR_FRAMESZ(15) sets the frame size to 16 bits (15 + 1).
  // Adding LPSPI_TCR_RXMSK masks the receiver, effectively disabling it. This ensures that no data is received during
  // SPI transmissions, which can improve performance for certain applications where data reception is unnecessary.
  // The updated mytcr is written back to the TCR registers of LPSPI4 and LPSPI3
  IMXRT_LPSPI4_S.TCR = (mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK;  //This will break all stock SPI transactions from this point on - disable receiver and go to 16 bit mode
  IMXRT_LPSPI3_S.TCR = (mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK;  //This will break all stock SPI transactions from this point on - disable receiver and go to 16 bit mode
                                                                                        // mytcr = (mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK;

  /* CFGR0: General SPI configuration register 0.
     CFGR1: General SPI configuration register 1.
     DER: Data enable register.
     IER: Interrupt enable register.
     TCR: Transfer control register. */

  /* Some debugging printfs when testing SPI
    Serial.print("CFGR0=:");
    Serial.println(IMXRT_LPSPI3_S.CFGR0);
    Serial.print("CFGR1=:");
    Serial.println(IMXRT_LPSPI3_S.CFGR1);
    Serial.print("DER=:");
    Serial.println(IMXRT_LPSPI3_S.DER);
    Serial.print("IER=:");
}
    Serial.println(IMXRT_LPSPI3_S.IER);
    Serial.print("TCR=");
    Serial.println(IMXRT_LPSPI3_S.TCR); */
#endif
}

//Finish the last SPI transactions
void SPI_flush() {
#ifdef VSTCM
  //Wait for the last transaction to finish and then set CS high from the last transaction
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  if (Spiflag)
    while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF))
      ;  //Loop until the last frame is complete
  if (Spi1flag)
    while (!(IMXRT_LPSPI3_S.SR & LPSPI_SR_FCF))
      ;                                //Loop until the last frame is complete
  digitalWriteFast(CS_R_G_X_Y, HIGH);  //Set the CS from the last transaction high
  digitalWriteFast(CS_B, HIGH);        //Set the CS from the last transaction high for the blue channel in case it was active (possibly use a flag to check??)

  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF;  //Clear the flag
  IMXRT_LPSPI3_S.SR = LPSPI_SR_FCF;  //Clear the flag
  Spiflag = 0;
  Spi1flag = 0;
#endif
}
