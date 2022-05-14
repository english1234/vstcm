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

The board was built with simplicity in mind using components that are easy to find, and easy to solder so that anyone can build one.

The programme code is a development of that which was provided with the original version of the v.st, with modifications made by "Swapfile" (Github user) to interface with AdvanceMAME, and then further modifications made by myself in order to add the new functionality specific to the vstcm. Ideally it should be rewritten or optimised by someone more familiar with the inner workings of the Teensy 4.1, and it is hoped that the publication on github will encourage contributions to develop this as a relatively cheap and easy solution for the vector arcade / vector graphics community.

For those interested in seeing how things got to this point, the development of the PCB is documented (in French, but use Google Translate if required) here : https://www.gamoover.net/Forums/index.php?topic=43469.0 (from page 5) and also shows previous work on building vector arcade HV boards, an Amplifone deflection reproduction, an Asteroids game PCB reproduction, a bit of yoke rewinding, etc.

If you want to get in touch to ask questions, or contribute, I can be contacted at: robin@robinchampion.com or on Github (user english1234), Gamoover (english2), UKVAC (english2), KLOV (english2), as well as hanging around on the various vector graphics/Vectrex forums on Facebook.

A dedicated vstcm web page can be found here: https://robinchampion.com/vst_colour_mod.htm
