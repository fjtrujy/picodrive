#ifndef PS2_CONFIG_H
#define PS2_CONFIG_H

int currentEmulationOpt(void);

int isShowFPSEnabled(void);
int isSoundEnabled(void);
int is8BitsConfig(void);
int is16BitsAccurate(void);
int isCDLedsEnabled(void);
int isVSYNCEnabled(void);
int isVSYNCModeEnabled(void);

void updateEmulationOpt(int newEmuOpt);

void prepareDefaultConfig(void);
void set16BtisConfig(void);
void set8BtisConfig(void);

#endif //PS2_CONFIG_H
