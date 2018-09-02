// port specific settings

#ifndef PS2_TIMING_H
#define PS2_TIMING_H

unsigned int ticksUS(void);
unsigned int ticksMS(void);
void delayMS(unsigned short int msec);
void delayCycles(unsigned short int cycles);
void waitTillUS(unsigned int us_to);

#endif //PS2_TIMING_H
