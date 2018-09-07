#ifndef IN_PS2_H
#define IN_PS2_H

void in_ps2_init(void *vdrv);
int in_ps2_update(void *drv_data, const int *binds, int *result);

#endif //IN_PS2_H
