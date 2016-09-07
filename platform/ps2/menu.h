// (c) Copyright 2006,2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

void menu_loop(void);
int  menu_loop_tray(void);
void menu_romload_prepare(const char *rom_name);
void menu_romload_end(void);


#define CONFIGURABLE_KEYS (PBTN_UP|PBTN_LEFT|PBTN_RIGHT|PBTN_DOWN| \
						   PBTN_L1|PBTN_R1|PBTN_L2|PBTN_R2|PBTN_L3|PBTN_R3| \
						   PBTN_TRIANGLE|PBTN_CIRCLE|PBTN_X|PBTN_SQUARE| \
						   PBTN_START| \
						   PBTN_NUB_L_UP|PBTN_NUB_L_RIGHT|PBTN_NUB_L_DOWN|PBTN_NUB_L_LEFT| \
						   PBTN_NUB_R_UP|PBTN_NUB_R_RIGHT|PBTN_NUB_R_DOWN|PBTN_NUB_R_LEFT)

