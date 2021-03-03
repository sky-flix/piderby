# piderby
Pinewood Derby Timer on Pi Pico

A Pi Pico microcontroller (RP2040) based Pinewood Derby Timer, with support for up to 8 lanes, servo controlled start gate, and 10us accuracy!

Interfaces with commonly available race manager software such as Grand Prix Race Manager, or the web based DerbyNet.

GPIO Pins 11-18 are hardware interrupts for triggering lanes 1-8, respectively. GPIO pin 22 is used for start gate switch control. SPDT microswitch is de-bounced
in hardware using a pair of NAND gates (74HC000) and two 10k ohm resistors (value is not critical), so as to prevent any erroneous interrupts. Finish line hardware is up to the consumer. During debugging, the hardware interrupts are pulled high using internal pullup resistors on the Pi Pico with a simple push button to bring the lane low to simulate a car crossing the finish line. In live application, they should be pulled LOW, and then brought high again via a "closed" lane switch. In that way, a lane triggers by breaking the connection, bringing the interrupt low and recording a time.

To increase timer consistency, an external ceramic oscillator is recommended, but is not absolutely required. Code is included to send times and other messages to Sparkfun opensegment 7 segment LEDS. Unfortunately, these units have now been discontinued from the manufacturer, but the schematics and code are still available should you wish to build your own. See https://www.sparkfun.com/products/retired/11647 for more information. A suitable replacement may be https://www.sparkfun.com/products/11442

Gate control is via a 5V miniature solenoid found on common sites such as Adafruit, Sparkfun or Mouser. Be sure to use an appropriately sized transistor or relay to power the solenoid. DO NOT POWER THE SOLENOID DIRECTLY FROM YOUR MICROCONTROLLER!
