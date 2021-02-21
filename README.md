# piderby
Pinewood Derby Timer on Pi Pico

A Pi Pico microcontroller (RP2040) based Pinewood Derby Timer, with support for up to 8 lanes, servo controlled start gate, and 10us accuracy!

Interfaces with commonly available race manager software such as Grand Prix Race Manager, or the web based DerbyNet, by emulating the eTekGadgets command set.

GPIO Pins 11-18 are hardware interrupts for triggering lanes 1-8, respectively. GPIO pin 22 is used for start gate switch control. SPDT microswitch is de-bounced
in hardware using a pair of NAND gates (74HC000) and two 10k ohm resistors (value is not critical), so as to prevent any erroneous interrupts. Finish line hardware is up to the consumer. During debugging, the hardware interrupts are pulled high using internal pullup resistors on the Pi Pico with a simple push button to bring the lane low to simulate a car crossing the finish lien. In live application, they should be pulled LOW, and then brought high again via a "closed" lane switch. In that way, a lane triggers by breaking the connection, bringing the interrupt low and recording a time.
