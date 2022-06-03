# VSTCM - the v.st Colour Mod - a colour vector graphics generator
The vstcm is a vector signal transceiver PCB which can generate colour vector graphics which can then be displayed on an oscilloscope or vector monitor (such as Amplifone, Wells Gardner WG6100 and Electrohome G05, as used in Star Wars, Tempest, Gravitar, etc.). Haven't got a vector monitor or an oscilloscope? Make your own, it's not that hard (see below)!

![vstcm pcb](http://robinchampion.com/vstcm/vstcmpcb.jpg)

When used with a Raspberry Pi (or Orange Pi) running AdvanceMAME, it can be used to play classic colour (and monochrome) vector arcade games.

![starwars](http://robinchampion.com/vstcm/starwars.jpg)

The original v.st was designed by Trammell Hudson for black & white games. Documentation for that version can be found here https://github.com/osresearch/vst and here https://trmm.net/V.st/

Compared to the original version, the new vstcm adds:
- RGB colour with different intensity levels
- a menu to change parameters
- programmable onboard control buttons
- programmable IR remote control buttons
- extra pots to control X & Y position
- several power source options, either USB, or external 5V/9V/12V
- an upgrade to the Teensy 4.1 for more power (eg >800Mhz as opposed to 120Mhz for the Teensy 3.2)

![testscreen1](http://robinchampion.com/vstcm/testscreen2.jpg)

The board was built with simplicity in mind using components that are easy to find, and easy to solder so that anyone can build one.

The programme code is a development of that which was provided with the original version of the v.st, with modifications made by "Swapfile" (Github user) to interface with AdvanceMAME, and then further modifications made by myself in order to add the new functionality specific to the vstcm. Ideally it should be rewritten or optimised by someone more familiar with the inner workings of the Teensy 4.1, and it is hoped that the publication on github will encourage contributions to develop this as a relatively cheap and easy solution for the vector arcade / vector graphics community.

For those interested in seeing how things got to this point, the development of the PCB is being documented with plenty of pictures (in French, but use Google Translate if required) here : https://www.gamoover.net/Forums/index.php?topic=43469.0 (from page 5) and also shows previous work on building vector arcade HV boards, an Amplifone deflection reproduction, an Asteroids game PCB reproduction, a bit of yoke rewinding, etc.

If you want to get in touch to ask questions, or contribute, I can be contacted at: robin@robinchampion.com or on Github (user english1234), Gamoover (english2), UKVAC (english2), KLOV (english2), as well as hanging around on the various vector graphics/Vectrex forums on Facebook.

A dedicated vstcm web page can be found here: https://robinchampion.com/vst_colour_mod.htm

# Getting the PCB built

A ZIP file is in the Gerbers directory. This can be uploaded to your PCB manufacturer of choice. It's a 2 sided 10cm x 10cm board so should be extremely cheap (JLPCB charged less than 5€ / $5 + shipping for 10 pieces in May 2022).

# Components

The BOM is in BOM teensyv.txt and has Mouser references for many parts at the right hand side. 

- IC2: You need to choose if you are going to use an external supply or not and if so what voltage to use (5V, 9V or 12V). Recom makes these parts, but so do Traco and there may be other brands. Make sure there is a D (for double) at the end of the model number which generates +/- voltages, rather than the S (single) version. As a guide, I started out with a 5V external supply and a RB-0512D.
- R12, R15 & R16: these resistors are in the op amp for the RGB signals and are marked as 68 ohm in the BOM, however the circuit was designed with 10K in those places. 10K is producing bent vectors on my setup, I'm currently experimenting with 68R and it's working pretty well. 
- The Molex parts are not strictly necessary, you may prefer something different or simply to solder wires directly to the holes in the PCB.
- U4: The TXS0108E	which converts voltage from 3.3V to 5V is described as having a DIP20 footprint. It's actually a bit wider than that, so I removed the DIP socket and used pin headers instead which I had to bend a little to get it to fit. This part was used as the previous Teensy 3.2 had 5V outputs whereas the Teensy 4.1 uses 3.3V. Theoretically, installing one of these avoided having to recalculate the values of the resistors in the Op Amp circuit, but since then I removed this part on my board and jumpered rows 2, 3, 4, 5 and 8 and everything seems to work fine with no further changes, so it can probably be left out as long as the Teensy is happy to produce the necessary voltage (a bit above 3.3V) to get the DACs to detect a high signal. 
- U5: See power options below. I have also used a 7805 as a direct replacement with a small heatsink on it, which gets quite warm but hasn't burnt out as yet.

 - optional parts: 
 1/ see power options below
 2/ see IR remote below

I would recommend socketing everything on the board (Teensy, DACs, Op Amps) so that they can be swapped out if better choices are found in the future.

Make sure you order short resistors, space for them on the board is tight.

# Power options for the PCB

The PCB can be powered in several ways:

1/ power the whole thing via USB from a Raspberry Pi: I could not get this to work, although I was using a long and cheap USB cable. It may work with a short good quality one. U5, C19 & C20 are not needed in this case.

2/ power the Teensy via USB from a Raspberry Pi, and use a separate external supply for the DACs and Op Amps. 
- 2a/ I tested this using a 1A 5V "wall wart" type supply connected to J6. U5, C19 & C20 are not needed in this case. 
- 2b/ Another option would be to leave out the RB-0512D and connect a +/-12V supply to J7. THIS HAS NOT YET BEEN TESTED.

3/ power the whole thing via an external supply which can be 9V or 12V (not 5V): this requires adding a LM2940T-5.0 regulator at U5 along with it's associated caps at C19 & C20, as well as cutting a link on the Teensy to ensure that it doesn't receive conflicting power from both the USB and the external supply. The RB-xx12D needs to be either a 9V or 12V model depending on the voltage of the external supply. A 5V supply won't work as the LM2940 requires over 6V to function according to its datasheet. 
WARNING: THE V2 OF THE PCB (AS OPPOSED TO V2.1 OR LATER) HAS THE SYMBOL FOR U5 INVERTED. PIN 1 OF THE LM2940 (THE LEFT HAND ONE) NEEDS TO BE AT THE TOP OF THE BOARD, THE SILKSCREEN ON THE PCB AND SCHEMATIC HAVE BEEN CORRECTED FROM V2.1 ONWARDS.

The choice is really just a matter of what you have on hand. It makes no difference what vector CRT you have, as the output voltages of the PCB are the same whatever you use to power it. If you're connecting to an existing arcade machine, then chances are you have a +/-12V supply coming out of the power brick. Otherwise, many people have a box of old "wall wart" 5V (or 9V or 12V) adapters which will work fine: either fit a barrel connector to the PCB input or chop the connector off the end of the power supply and solder the 2 wires directly to the board (check with a multimeter first which is positive and which is ground). 

The easiest option is to use a 5V (option 2a) as the LM2940 is not required, and you don't need to cut the link on the Teensy to separate USB and external power.

# Schematic

This may seem obvious, but it's worth downloading Kicad in order to view the schematic and the PCB as there are some build notes on the schematic, and it will help you to understand the main sections of the PCB and how it all connects together. 

# Programming the Teensy

Follow the instructions on this page to download and install the Arduino environment and Teensyduino extension: https://www.pjrc.com/teensy/td_download.html
Use the Arduino software to load the .ino file in the Teensy code directory.
Connect the Teensy via USB to your computer.
Choose the Teensy 4.1 in the Teensyduino options, and overclock to 816Mhz, as well as the "Fastest" option.
Press the compile button.
Press the upload button (or the button on the Teensy if it doesn't upload automatically).
(You can also use PlatformIO if you prefer).

# Testing the vstcm

Once the board is built and the Teensy programmed and fitted, it can be connected to your deflection board of choice (it has only been tested on an Amplifone so far, but should work on WG6100 and others, including B&W vectors) and when powered on should show a test screen. It may be necessary to change the size and positions pots on the PCB as well as other controls on the deflection board (such as Z or colour gain).

Before connecting to the deflection board, it would probably be a good idea to make sure the vstcm is generating appropriate voltages at its outputs (preferably with an oscilloscope, or failing that with a decent multimeter). 

![typicalsetup](http://robinchampion.com/vstcm/typicalsetup.jpg)

# Testing games with AdvanceMAME

A Raspberry Pi 4 or 400 is recommended (I have also tested with a Pi 3 Model B+ 2017 and an Orange Pi 3 LTS which seem to work ok too). If you are running the vstcm from the Raspberry/Orange Pi then a 3A supply would be preferable.
Other options (which I have not yet tested) are PC (either Windows or a Linux VM under Windows, or native Linux) or Mac. 
I followed the instructions here to download and compile AdvanceMAME: https://www.arcade-projects.com/threads/almost-pixel-perfect-arcade-emulation-on-raspberry-pi-with-advancemame.7777/

![orangepi](http://robinchampion.com/vstcm/orangepi.jpg)

Basically, there are just 7 commands on the Pi which are as follows:

sudo apt-get install git autoconf automake libsdl2-dev libasound2-dev libfreetype6-dev zlib1g-dev libexpat1-dev libslang2-dev libncurses5-dev

git clone https://github.com/amadvance/advancemame.git

cd advancemame

sh autogen.sh

./configure

make -j3

sudo make install


You need to find some ROMs from somewhere and copy them into the ROM folder. I'm sure you'll manage to find them... The easiest way to get them on to the Pi is to set up a Samba share and copy them over from a PC.

Then to launch, type advmame followed by the name of the game.

If you're not getting output over the USB cable to the vstcm, then check the advmame.rc file and at the bottom make sure you have vector_aux_renderer set to dvg (instead of none) and vector_aux_renderer_port set to /dev/ttyACM0

![gravitar](http://robinchampion.com/vstcm/gravitar.jpg)![tempest](http://robinchampion.com/vstcm/tempest.jpg)

# Interface between vstcm and AdvanceMAME

The AdvanceMAME protocol for the USB DVG is here: https://github.com/amadvance/advancemame/blob/master/advance/osd/dvg.c 

# IR remote programmable control buttons

For £1 / $1 / 1€ you can get a HX1838 infra red adapter board with remote control, wiring, everything you need in fact. There are only 3 wires to connect: 5V, GND and signal. I soldered pin headers in the holes provided on either side of the Teensy, and put 5V on the bottom left hand hole of the Teensy (to left of USB socket), GND on the bottom right pin of the Teensy, and signal on pin 32 (top right hand pin of Teensy). 

This might seem like a bit of a gadget, but when you are spending your time going behind the arcade cab to press a button, coming back round the front to see the effect on the screen, and doing this over and over again, the advantages become obvious! 

The IR sensor is the size of an LED and so very easy to hide somewhere at the front of an arcade cab, and then you can just change the settings at will while you look at the screen.

![IR1](http://robinchampion.com/vstcm/IR1.jpg)![ir2](http://robinchampion.com/vstcm/IR2.jpg)

# Haven't got a vector monitor or an oscilloscope?

Apart from this PCB, you need a CRT, a high voltage board, a deflection board and a power supply to make a complete vector monitor. There are solutions for all of these parts:

CRT - I'm using a 19"/48cm Philips TV bought for pennies through the small ads. You need to rewind the yoke using magnet wire. There is an excellent video by Jason Kopp here which explains all: https://youtu.be/Ci9qiGVMF7s I experimented on a 5" b&w security monitor first, then went for the big screen. Avoid Trinitrons and PC monitors, you need a basic no frills TV tube. It took me several attempts to get it more or less right, but I still have some wires which are not completely straight and the result seems to be the slightly bent vectors visible in the top right hand corner of my screen in the photo of the test screen above. The whole thing really wasn't hard, it just requires patience.

High voltage: Amplifone and Wells Gardner 6100 blank PCBs are available online, and there are also fully built solutions (see the various vector groups on Facebook) available new or second hand from the usual auction sites and specialist arcade sellers (mostly USA based).

Deflection board: I bought a second hand non working Amplifone and then fixed it (which wasn't hard, plenty of info online), but new PCBs are available online, and a Wells Gardner WG6100 should work too.

Power supply: the CRT and deflection board can be run either from an old Atari power brick, or else by wiring together two cheap 24V power supplies from Aliexpress in order to provide +/- 24V which satisfies the requirement for 50VAC on a Amplifone colour vector monitor. The CRT needs 6.3V for the heater filament, which I'm getting from an Atari power brick using the supply meant for the coin door, but there are other solutions if you look online (hint: 6.3V is frequently required for valve amp projects). If you find the right Atari power brick, it will power the whole thing: CRT, HV, deflection, Raspberry Pi and vstcm.

There's plenty of discussion of solutions to these problems on KLOV, UKVAC, and the FB vector forums with plenty of knowledgeable people able to answer questions. Also there are a fair few videos on Youtube going from theory to practice. Again, it's not that hard, so give it a go!
