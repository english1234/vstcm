# VSTCM - the v.st Colour Mod - a colour vector graphics generator
The vstcm is a PCB which can generate colour vector graphics which can then be displayed on an oscilloscope or vector monitor (such as Amplifone, Wells Gardner WG6100 and Electrohome G05, as used in Star Wars, Tempest, Gravitar, etc.).

![vstcm pcb](http://robinchampion.com/vstcm/vstcmpcb.jpg)

When used with a Raspberry Pi running AdvanceMAME, it can be used to play classic colour (and monochrome) vector arcade games.

![starwars](http://robinchampion.com/vstcm/starwars.jpg)

The original v.st was designed by Trammell Hudson for black & white games. Documentation for that version can be found here https://github.com/osresearch/vst and here https://trmm.net/V.st/

Compared to the original version, the new vstcm adds:
- RGB colour with different intensity levels
- a menu to change parameters
- programmable control buttons
- extra pots to control X & Y position
- several power source options, either USB, or external 5V/9V/12V
- an upgrade to the Teensy 4.1 for more power

![testscreen](http://robinchampion.com/vstcm/testscreen.jpg)

The board was built with simplicity in mind using components that are easy to find, and easy to solder so that anyone can build one.

The programme code is a development of that which was provided with the original version of the v.st, with modifications made by "Swapfile" (Github user) to interface with AdvanceMAME, and then further modifications made by myself in order to add the new functionality specific to the vstcm. Ideally it should be rewritten or optimised by someone more familiar with the inner workings of the Teensy 4.1, and it is hoped that the publication on github will encourage contributions to develop this as a relatively cheap and easy solution for the vector arcade / vector graphics community.

IF YOU DECIDE TO BUILD THE BOARD, BE AWARE THAT AS IT STANDS, THE REFRESH RATE IS NOT SUFFICIENT FOR COMPLEX GAMES SUCH AS STAR WARS. WHEN THE CODE WAS PORTED FROM TEENSY 3.2, THE DMA FUNCTIONALITY WAS DISABLED AS IT REQUIRES A REWRITE FOR THE NEW ARCHITECTURE WHICH REDUCES THE FPS. CURRENTLY THE CODE EXECUTES DATA TRANSFER VIA SPI. ANY TEENSY 4.1 GURUS WHO CAN HELP TO RESTORE DMA, PLEASE FEEL FREE TO CONTRIBUTE!

For those interested in seeing how things got to this point, the development of the PCB is documented (in French, but use Google Translate if required) here : https://www.gamoover.net/Forums/index.php?topic=43469.0 (from page 5) and also shows previous work on building vector arcade HV boards, an Amplifone deflection reproduction, an Asteroids game PCB reproduction, a bit of yoke rewinding, etc.

If you want to get in touch to ask questions, or contribute, I can be contacted at: robin@robinchampion.com or on Github (user english1234), Gamoover (english2), UKVAC (english2), KLOV (english2), as well as hanging around on the various vector graphics/Vectrex forums on Facebook.

A dedicated vstcm web page can be found here: https://robinchampion.com/vst_colour_mod.htm

![gravitar](http://robinchampion.com/vstcm/gravitar.jpg)![tempest](http://robinchampion.com/vstcm/tempest.jpg)

# Getting the PCB built

A ZIP file is in the Gerbers directory. This can be uploaded to your PCB manufacturer of choice. It's a 2 sided 10cm x 10cm board so should be extremely cheap (JLPCB charged less than 5€ / $5 + shipping for 10 pieces in May 2022).

# Components

The BOM is in BOM teensyv.txt and has Mouser references for many parts at the right hand side. 

- IC2: You need to choose if you are going to use an external supply or not and if so what voltage to use (5V, 9V or 12V). Recom makes these parts, but so do Traco and there may be other brands. Make sure there is a D (for double) at the end of the model number which generates +/- voltages, rather than the S (single) version. As a guide, I started out with a 5V external supply and a RB-0512D.
- R12, R15 & R16: these are marked as 68 ohm in the BOM, however the circuit was designed with 10K in those places. 10K is probably a better choice, but 68R is what I'm currently experimenting with. 
- The Molex parts are not strictly necessary, you may prefer something different or simply to solder wires directly to the holes in the PCB.
- U4 : The TXS0108E	which converts voltage from 3.3V to 5V is described as having a DIP20 footprint. It's actually a bit wider than that, so I removed the DIP socket and used pin headers instead which I had to bend a little to get it to fit. This part was used as the previous Teensy 3.2 had 5V outputs whereas the Teensy 4.1 uses 3.3V. Installing one of these avoided having to recalculate the values of the resistors in the Op Amp circuit, but if someone works out suitable values for all the resistors in the RGB and XY amplifier sections of the schematic, then U4 can be omitted and jumpered instead. 

 - optional parts: see power options below

I would recommend socketing everything on the board (Teensy, DACs, Op Amps) so that they can be swapped out if better choices are found in the future.

# Power options for the PCB

The PCB can be powered in several ways:
- power the whole thing via USB from a Raspberry Pi: I could not get this to work, although I was using a long and cheap USB cable. It may work with a short good quality one. U5, C19 & C20 are not needed.
- power the Teensy via USB from a Raspberry Pi, and use a separate external supply for the DACs and Op Amps. I tested this using a 1A 5V "wall wart" type supply connected to J6. U5, C19 & C20 are not needed. Another option would be to leave out the RB-0512D and connect a +/-12V supply to J7. This has not yet been tested.
- power the whole thing via an external supply which can be 9V or 12V (not 5V): this requires adding a LM2940T-5.0 regulator at U5 along with it's associated caps at C19 & C20, as well as cutting a link on the Teensy to ensure that it doesn't receive conflicting power from both the USB and the external supply. The RB-xx12D needs to be either a 9V or 12V model depending on the voltage of the external supply. This has not yet been tested either. A 5V supply won't work as the LM2940 requires over 6V to function according to its datasheet.

# Schematic

This may seem obvious, but it's worth downloading Kicad in order to view the schematic and the PCB as there are some build notes on the schematic, and it will help you to understand the main sections of the PCB and how it all connects together. 

# Programming the Teensy

Follow the instructions on this page to download and install the Arduino environment and Teensyduino extension: https://www.pjrc.com/teensy/td_download.html
Use the Arduino software to load the .ino file in the Teensy code directory
Connect the Teensy via USB to your computer
Press the compile button
Press the upload button (or the button on the Teensy if it doesn't upload automatically)
You can also use PlatformIO if you prefer.

# Testing the vstcm

Once the board is built and the Teensy programmed and fitted, it can be connected to your deflection board of choice (it has only been tested on an Amplifone so far) and when powered on should show a test screen. It may be necessary to change the size and positions pots on the PCB as well as other controls on the deflection board (such as Z or colour gain).

# Testing games with AdvanceMAME

A Raspberry Pi 4 is recommended (I only have a RP3 Model B 2017 which seems to struggle at times). I have an Orange Pi on order and will report back on whether it is any better (it's certainly cheaper right now). If you are running the vstcm from the Raspberry Pi then a 3A supply would be preferable.
Other options (which I have not yet tested) are PC (either Windows or a Linux VM under Windows, or native Linux) or Mac. 
I followed the instructions here to download and compile AdvanceMAME: https://www.arcade-projects.com/threads/almost-pixel-perfect-arcade-emulation-on-raspberry-pi-with-advancemame.7777/

You need to find some ROMs from somewhere. I'm sure you'll manage...

In order to get games to work, I started the Pi first while connected to a HDMI screen and then started a game. I plugged in the vstcm and turned on the vector monitor, and finally connected the USB cable to the vstcm. I think there is some sort of handshake that the vstcm code doesn't yet handle which stops MAME from working if it is plugged in straightaway. There is something of the sort in the AdvanceMAME protocol for the USB DVG here: https://github.com/amadvance/advancemame/blob/master/advance/osd/dvg.c 

