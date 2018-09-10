#ifndef PS2_PICO_H
#define PS2_PICO_H

int currentPicoOpt(void);

int isPicoOptMCDCDDAEnabled(void);
int isPicoOptStereoEnabled(void);
int isPicoOptFullAudioEnabled(void);

void setPicoOptNormalRendered(void);
void setPicoOptAlternativeRendered(void);
void setPicoOptAccSprites(void);
void setPicoOpt(int newOpt);
void picoOptUpdateOpt(int newOpt);

#endif //PS2_PICO_H