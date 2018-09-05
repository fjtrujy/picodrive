#ifndef PS2_PICO_H
#define PS2_PICO_H

int currentPicoOpt(void);

int isPicoOptAlternativeRenderedEnabled(void);
int isPicoOptMCDCDDAEnabled(void);
int isPicoOptStereoEnabled(void);
int isPicoOptFullAudioEnabled(void);

void setPicoOptAccSprites(void);
void setPicoOpt(int newOpt);
void picoOptUpdateOpt(int newOpt);

#endif //PS2_PICO_H
