#ifndef PS2_CONFIG_H
#define PS2_CONFIG_H

int currentEmulationOpt(void);

int isShowFPSEnabled(void);
int isSoundEnabled(void);
int is8BitsAccurate(void);
int is8bitsFast(void);
int is16Bits(void);
int isCDLedsEnabled(void);
int isVSYNCEnabled(void);
int isVSYNCModeEnabled(void);
int get_renderer(void);

void updateEmulationOpt(int newEmuOpt);

void prepareDefaultConfig(void);
void change_renderer(int diff);

#endif //PS2_CONFIG_H
