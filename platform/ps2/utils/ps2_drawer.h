#ifndef PS2_DRAWER_H
#define PS2_DRAWER_H

void emuDrawerPrepareConfig(void);
void emuDrawerUpdateConfig(void);
void emuDrawerShowInfo(const char *fps, const char *notice, int lagging_behind);

#endif //PS2_DRAWER_H
