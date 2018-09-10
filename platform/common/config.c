/*
 * Human-readable config file management for PicoDrive
 * (c) notaz, 2008
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef __EPOC32__
#include <unistd.h>
#endif
#include "config.h"
#include "plat.h"
#include "input.h"
#include "lprintf.h"
#include "posix.h"

static char *mystrip(char *str);

#ifndef _MSC_VER

#include "menu.h"
#include "emu.h"
#include <pico/pico.h>

#define NL "\n"
// always output DOS endlines
#ifdef _WIN32
#define NL "\n"
#else
#endif

static int seek_sect(FILE *f, const char *section)
{
	char line[128], *tmp;
	int len;

	len = strlen(section);
	// seek to the section needed
	while (!feof(f))
	{
		tmp = fgets(line, sizeof(line), f);
		if (tmp == NULL) break;

		if (line[0] != '[') continue; // not section start
		if (strncmp(line + 1, section, len) == 0 && line[len+1] == ']')
			return 1; // found it
	}

	return 0;
}

static void keys_write(FILE *fn, const char *bind_str, int dev_id, const int *binds, int no_defaults)
{
	char act[48];
	int key_count, k, i;
	const int *def_binds;

	key_count = in_get_dev_info(dev_id, IN_INFO_BIND_COUNT);
	def_binds = in_get_dev_def_binds(dev_id);

	for (k = 0; k < key_count; k++)
	{
		const char *name;
		int t, mask;
		act[0] = act[31] = 0;

		for (t = 0; t < IN_BINDTYPE_COUNT; t++)
			if (binds[IN_BIND_OFFS(k, t)] != def_binds[IN_BIND_OFFS(k, t)])
				break;

		if (no_defaults && t == IN_BINDTYPE_COUNT)
			continue;	/* no change from defaults */

		name = in_get_key_name(dev_id, k);

		for (t = 0; t < IN_BINDTYPE_COUNT; t++)
			if (binds[IN_BIND_OFFS(k, t)] != 0 || def_binds[IN_BIND_OFFS(k, t)] == 0)
				break;

		if (t == IN_BINDTYPE_COUNT) {
			/* key has default bind removed */
			fprintf(fn, "%s %s =" NL, bind_str, name);
			continue;
		}

		for (i = 0; i < sizeof(me_ctrl_actions) / sizeof(me_ctrl_actions[0]); i++) {
			mask = me_ctrl_actions[i].mask;
			if (mask & binds[IN_BIND_OFFS(k, IN_BINDTYPE_PLAYER12)]) {
				strncpy(act, me_ctrl_actions[i].name, 31);
				fprintf(fn, "%s %s = player1 %s" NL, bind_str, name, mystrip(act));
			}
			mask = me_ctrl_actions[i].mask << 16;
			if (mask & binds[IN_BIND_OFFS(k, IN_BINDTYPE_PLAYER12)]) {
				strncpy(act, me_ctrl_actions[i].name, 31);
				fprintf(fn, "%s %s = player2 %s" NL, bind_str, name, mystrip(act));
			}
		}

		for (i = 0; emuctrl_actions[i].name != NULL; i++) {
			mask = emuctrl_actions[i].mask;
			if (mask & binds[IN_BIND_OFFS(k, IN_BINDTYPE_EMU)]) {
				strncpy(act, emuctrl_actions[i].name, 31);
				fprintf(fn, "%s %s = %s" NL, bind_str, name, mystrip(act));
			}
		}
	}
}

/* XXX: this should go to menu structures instead */
static int default_var(const menu_entry *me)
{
	switch (me->id)
	{
		case MA_OPT2_ENABLE_YM2612:
		case MA_OPT2_ENABLE_SN76496:
		case MA_OPT2_ENABLE_Z80:
		case MA_OPT_6BUTTON_PAD:
		case MA_OPT_ACC_SPRITES:
		case MA_OPT_ARM940_SOUND:
		case MA_CDOPT_PCM:
		case MA_CDOPT_CDDA:
		case MA_CDOPT_SCALEROT_CHIP:
		case MA_CDOPT_BETTER_SYNC:
		case MA_CDOPT_SAVERAM:
		case MA_32XOPT_ENABLE_32X:
		case MA_32XOPT_PWM:
		case MA_OPT2_SVP_DYNAREC:
		case MA_OPT2_NO_SPRITE_LIM:
		case MA_OPT2_NO_IDLE_LOOPS:
			return defaultConfig.s_PicoOpt;

		case MA_OPT_SRAM_STATES:
		case MA_OPT_SHOW_FPS:
		case MA_OPT_ENABLE_SOUND:
		case MA_OPT2_GZIP_STATES:
		case MA_OPT2_SQUIDGEHACK:
		case MA_OPT2_NO_LAST_ROM:
		case MA_OPT2_RAMTIMINGS:
		case MA_CDOPT_LEDS:
		case MA_OPT2_A_SN_GAMMA:
		case MA_OPT2_VSYNC:
		case MA_OPT_INTERLACED:
		case MA_OPT2_DBLBUFF:
		case MA_OPT2_STATUS_LINE:
		case MA_OPT2_NO_FRAME_LIMIT:
		case MA_OPT_TEARING_FIX:
			return defaultConfig.EmuOpt;

		case MA_CTRL_TURBO_RATE: return defaultConfig.turbo_rate;
		case MA_OPT_SCALING:     return defaultConfig.scaling;
		case MA_OPT_ROTATION:    return defaultConfig.rotation;
		case MA_OPT2_GAMMA:      return defaultConfig.gamma;
		case MA_OPT_FRAMESKIP:   return defaultConfig.Frameskip;
		case MA_OPT_CONFIRM_STATES: return defaultConfig.confirm_save;
		case MA_OPT_CPU_CLOCKS:  return defaultConfig.CPUclock;
		case MA_OPT_RENDERER:    return defaultConfig.renderer;
		case MA_32XOPT_RENDERER: return defaultConfig.renderer32x;

		case MA_OPT_SAVE_SLOT:
			return 0;

		default:
			lprintf("missing default for %d\n", me->id);
			return 0;
	}
}

static int is_cust_val_default(const menu_entry *me)
{
	switch (me->id)
	{
		case MA_OPT_REGION:
			return defaultConfig.s_PicoRegion == PicoRegionOverride &&
				defaultConfig.s_PicoAutoRgnOrder == PicoAutoRgnOrder;
		case MA_OPT_SOUND_QUALITY:
			return defaultConfig.s_PsndRate == PsndRate &&
				((defaultConfig.s_PicoOpt ^ PicoOpt) & POPT_EN_STEREO) == 0;
		case MA_CDOPT_READAHEAD:
			return defaultConfig.s_PicoCDBuffers == PicoCDBuffers;
		case MA_32XOPT_MSH2_CYCLES:
			return p32x_msh2_multiplier == MSH2_MULTI_DEFAULT;
		case MA_32XOPT_SSH2_CYCLES:
			return p32x_ssh2_multiplier == SSH2_MULTI_DEFAULT;
		default:break;
	}

	lprintf("is_cust_val_default: unhandled id %i\n", me->id);
	return 0;
}

int config_writesect(const char *fname, const char *section)
{
	FILE *fo = NULL, *fn = NULL; // old and new
	int no_defaults = 0; // avoid saving defaults
	menu_entry *me;
	int t, tlen, ret;
	char line[128], *tmp;

	if (section != NULL)
	{
		no_defaults = 1;

		fo = fopen(fname, "r");
		if (fo == NULL) {
			fn = fopen(fname, "w");
			goto write;
		}

		ret = seek_sect(fo, section);
		if (!ret) {
			// sect not found, we can simply append
			fclose(fo); fo = NULL;
			fn = fopen(fname, "a");
			goto write;
		}

		// use 2 files..
		fclose(fo);
		rename(fname, "tmp.cfg");
		fo = fopen("tmp.cfg", "r");
		fn = fopen(fname, "w");
		if (fo == NULL || fn == NULL) goto write;

		// copy everything until sect
		tlen = strlen(section);
		while (!feof(fo))
		{
			tmp = fgets(line, sizeof(line), fo);
			if (tmp == NULL) break;

			if (line[0] == '[' && strncmp(line + 1, section, tlen) == 0 && line[tlen+1] == ']')
				break;
			fputs(line, fn);
		}

		// now skip to next sect
		while (!feof(fo))
		{
			tmp = fgets(line, sizeof(line), fo);
			if (tmp == NULL) break;
			if (line[0] == '[') {
				fseek(fo, -strlen(line), SEEK_CUR);
				break;
			}
		}
		if (feof(fo))
		{
			fclose(fo); fo = NULL;
			remove("tmp.cfg");
		}
	}
	else
	{
		fn = fopen(fname, "w");
	}

write:
	if (fn == NULL) {
		if (fo) fclose(fo);
		return -1;
	}
	if (section != NULL)
		fprintf(fn, "[%s]" NL, section);

	for (me = me_list_get_first(); me != NULL; me = me_list_get_next())
	{
		int dummy;
		if (!me->need_to_save)
			continue;

		if (me->beh == MB_OPT_ONOFF || me->beh == MB_OPT_CUSTONOFF) {
			if (!no_defaults || ((*(int *)me->var ^ default_var(me)) & me->mask))
				fprintf(fn, "%s = %i" NL, me->name, (*(int *)me->var & me->mask) ? 1 : 0);
		}
		else if (me->beh == MB_OPT_RANGE || me->beh == MB_OPT_CUSTRANGE) {
			if (!no_defaults || (*(int *)me->var ^ default_var(me)))
				fprintf(fn, "%s = %i" NL, me->name, *(int *)me->var);
		}
		else if (me->beh == MB_OPT_ENUM && me->data != NULL) {
			const char **names = (const char **)me->data;
			for (t = 0; names[t] != NULL; t++)
				if (*(int *)me->var == t && (!no_defaults || (*(int *)me->var ^ default_var(me)))) {
					strncpy(line, names[t], sizeof(line));
					goto write_line;
				}
		}
		else if (me->name != NULL && me->generate_name != NULL) {
			if (!no_defaults || !is_cust_val_default(me)) {
				strncpy(line, me->generate_name(0, &dummy), sizeof(line));
				goto write_line;
			}
		}
		else
			lprintf("config: unhandled write: %i\n", me->id);
		continue;

write_line:
		line[sizeof(line) - 1] = 0;
		mystrip(line);
		fprintf(fn, "%s = %s" NL, me->name, line);
	}

	/* input: save device names */
	for (t = 0; t < IN_MAX_DEVS; t++)
	{
		const int  *binds = in_get_dev_binds(t);
		const char *name =  in_get_dev_name(t, 0, 0);
		if (binds == NULL || name == NULL)
			continue;

		fprintf(fn, "input%d = %s" NL, t, name);
	}

	/* input: save binds */
	for (t = 0; t < IN_MAX_DEVS; t++)
	{
		const int *binds = in_get_dev_binds(t);
		const char *name = in_get_dev_name(t, 0, 0);
		char strbind[16];
		int count;

		if (binds == NULL || name == NULL)
			continue;

		sprintf(strbind, "bind%d", t);
		if (t == 0) strbind[4] = 0;

		count = in_get_dev_info(t, IN_INFO_BIND_COUNT);
		keys_write(fn, strbind, t, binds, no_defaults);
	}

#if !defined(PSP) || !defined(_EE)
	if (section == NULL)
		fprintf(fn, "Sound Volume = %i" NL, currentConfig.volume);
#endif

	fprintf(fn, NL);

	if (fo != NULL)
	{
		// copy whatever is left
		while (!feof(fo))
		{
			tmp = fgets(line, sizeof(line), fo);
			if (tmp == NULL) break;

			fputs(line, fn);
		}
		fclose(fo);
		remove("tmp.cfg");
	}

	fclose(fn);
	return 0;
}


int config_writelrom(const char *fname)
{
	char line[128], *tmp, *optr = NULL;
	char *old_data = NULL;
	int size;
	FILE *f;

	if (strlen(rom_fname_loaded) == 0) return -1;

	f = fopen(fname, "r");
	if (f != NULL)
	{
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fseek(f, 0, SEEK_SET);
		old_data = malloc(size + size/8);
		if (old_data != NULL)
		{
			optr = old_data;
			while (!feof(f))
			{
				tmp = fgets(line, sizeof(line), f);
				if (tmp == NULL) break;
				mystrip(line);
				if (strncasecmp(line, "LastUsedROM", 11) == 0)
					continue;
				sprintf(optr, "%s", line);
				optr += strlen(optr);
			}
		}
		fclose(f);
	}

	f = fopen(fname, "w");
	if (f == NULL) return -1;

	if (old_data != NULL) {
		fwrite(old_data, 1, optr - old_data, f);
		free(old_data);
	}
	fprintf(f, "LastUsedROM = %s" NL, rom_fname_loaded);
	fclose(f);
	return 0;
}

/* --------------------------------------------------------------------------*/

int config_readlrom(const char *fname)
{
	char line[128], *tmp;
	int i, len, ret = -1;
	FILE *f;

	f = fopen(fname, "r");
	if (f == NULL) return -1;

	// seek to the section needed
	while (!feof(f))
	{
		tmp = fgets(line, sizeof(line), f);
		if (tmp == NULL) break;

		if (strncasecmp(line, "LastUsedROM", 11) != 0) continue;
		len = strlen(line);
		for (i = 0; i < len; i++)
			if (line[i] == '#' || line[i] == '\r' || line[i] == '\n') { line[i] = 0; break; }
		tmp = strchr(line, '=');
		if (tmp == NULL) break;
		tmp++;
		mystrip(tmp);

		len = sizeof(rom_fname_loaded);
		strncpy(rom_fname_loaded, tmp, len);
		rom_fname_loaded[len-1] = 0;
		ret = 0;
		break;
	}
	fclose(f);
	return ret;
}


static int custom_read(menu_entry *me, const char *var, const char *val)
{
	char *tmp;

	switch (me->id)
	{
		case MA_OPT_FRAMESKIP:
			if (strcasecmp(var, "Frameskip") != 0) return 0;
			if (strcasecmp(val, "Auto") == 0)
			     currentConfig.Frameskip = -1;
			else currentConfig.Frameskip = atoi(val);
			return 1;

		case MA_OPT_SOUND_QUALITY:
			if (strcasecmp(var, "Sound Quality") != 0) return 0;
			PsndRate = strtoul(val, &tmp, 10);
			if (PsndRate < 8000 || PsndRate > 44100)
				PsndRate = 22050;
			if (*tmp == 'H' || *tmp == 'h') tmp++;
			if (*tmp == 'Z' || *tmp == 'z') tmp++;
			while (*tmp == ' ') tmp++;
			if        (strcasecmp(tmp, "stereo") == 0) {
				PicoOpt |=  POPT_EN_STEREO;
			} else if (strcasecmp(tmp, "mono") == 0) {
				PicoOpt &= ~POPT_EN_STEREO;
			} else
				return 0;
			return 1;

		case MA_OPT_REGION:
			if (strcasecmp(var, "Region") != 0) return 0;
			if       (strncasecmp(val, "Auto: ", 6) == 0)
			{
				const char *p = val + 5, *end = val + strlen(val);
				int i;
				PicoRegionOverride = PicoAutoRgnOrder = 0;
				for (i = 0; p < end && i < 3; i++)
				{
					while (*p == ' ') p++;
					if        (p[0] == 'J' && p[1] == 'P') {
						PicoAutoRgnOrder |= 1 << (i*4);
					} else if (p[0] == 'U' && p[1] == 'S') {
						PicoAutoRgnOrder |= 4 << (i*4);
					} else if (p[0] == 'E' && p[1] == 'U') {
						PicoAutoRgnOrder |= 8 << (i*4);
					}
					while (*p != ' ' && *p != 0) p++;
					if (*p == 0) break;
				}
			}
			else   if (strcasecmp(val, "Auto") == 0) {
				PicoRegionOverride = 0;
			} else if (strcasecmp(val, "Japan NTSC") == 0) {
				PicoRegionOverride = 1;
			} else if (strcasecmp(val, "Japan PAL") == 0) {
				PicoRegionOverride = 2;
			} else if (strcasecmp(val, "USA") == 0) {
				PicoRegionOverride = 4;
			} else if (strcasecmp(val, "Europe") == 0) {
				PicoRegionOverride = 8;
			} else
				return 0;
			return 1;

		case MA_OPT2_GAMMA:
			if (strcasecmp(var, "Gamma correction") != 0) return 0;
			currentConfig.gamma = (int) (atof(val) * 100.0);
			return 1;

		case MA_CDOPT_READAHEAD:
			if (strcasecmp(var, "ReadAhead buffer") != 0) return 0;
			PicoCDBuffers = atoi(val) / 2;
			return 1;

		case MA_32XOPT_MSH2_CYCLES:
		case MA_32XOPT_SSH2_CYCLES: {
			int *mul = (me->id == MA_32XOPT_MSH2_CYCLES) ? &p32x_msh2_multiplier : &p32x_ssh2_multiplier;
			*mul = ((unsigned int)atoi(val) << SH2_MULTI_SHIFT) / 7670;
			return 1;
		}

		/* PSP */
		case MA_OPT3_SCALE:
			if (strcasecmp(var, "Scale factor") != 0) return 0;
			currentConfig.scale = atof(val);
			return 1;
		case MA_OPT3_HSCALE32:
			if (strcasecmp(var, "Hor. scale (for low res. games)") != 0) return 0;
			currentConfig.hscale32 = atof(val);
			return 1;
		case MA_OPT3_HSCALE40:
			if (strcasecmp(var, "Hor. scale (for hi res. games)") != 0) return 0;
			currentConfig.hscale40 = atof(val);
			return 1;
		case MA_OPT3_VSYNC:
			// XXX: use enum
			if (strcasecmp(var, "Wait for vsync") != 0) return 0;
			if        (strcasecmp(val, "never") == 0) {
				currentConfig.EmuOpt &= ~0x12000;
			} else if (strcasecmp(val, "sometimes") == 0) {
				currentConfig.EmuOpt |=  0x12000;
			} else if (strcasecmp(val, "always") == 0) {
				currentConfig.EmuOpt &= ~0x12000;
				currentConfig.EmuOpt |=  0x02000;
			} else
				return 0;
			return 1;

		default:
			lprintf("unhandled custom_read %i: %s\n", me->id, var);
			return 0;
	}
}


static unsigned int keys_encountered = 0;

static int parse_bind_val(const char *val, int *type)
{
	int i;

	*type = IN_BINDTYPE_NONE;
	if (val[0] == 0)
		return 0;
	
	if (strncasecmp(val, "player", 6) == 0)
	{
		int player, shift = 0;
		player = atoi(val + 6) - 1;

		if (player > 1)
			return -1;
		if (player == 1)
			shift = 16;

		*type = IN_BINDTYPE_PLAYER12;
		for (i = 0; i < sizeof(me_ctrl_actions) / sizeof(me_ctrl_actions[0]); i++) {
			if (strncasecmp(me_ctrl_actions[i].name, val + 8, strlen(val + 8)) == 0)
				return me_ctrl_actions[i].mask << shift;
		}
	}
	for (i = 0; emuctrl_actions[i].name != NULL; i++) {
		if (strncasecmp(emuctrl_actions[i].name, val, strlen(val)) == 0) {
			*type = IN_BINDTYPE_EMU;
			return emuctrl_actions[i].mask;
		}
	}

	return -1;
}

static void keys_parse(const char *key, const char *val, int dev_id)
{
	int acts, type;

	acts = parse_bind_val(val, &type);
	if (acts == -1) {
		lprintf("config: unhandled action \"%s\"\n", val);
		return;
	}

	in_config_bind_key(dev_id, key, acts, type);
}

static int get_numvar_num(const char *var)
{
	char *p = NULL;
	int num;
	
	if (var[0] == ' ')
		return 0;

	num = strtoul(var, &p, 10);
	if (*p == 0 || *p == ' ')
		return num;

	return -1;
}

/* map dev number in confing to input dev number */
static unsigned char input_dev_map[IN_MAX_DEVS];

static void parse(const char *var, const char *val)
{
	menu_entry *me;
	int tmp;

	if (strcasecmp(var, "LastUsedROM") == 0)
		return; /* handled elsewhere */

	if (strcasecmp(var, "Sound Volume") == 0) {
		currentConfig.volume = atoi(val);
		return;
	}

	/* input: device name */
	if (strncasecmp(var, "input", 5) == 0) {
		int num = get_numvar_num(var + 5);
		if (num >= 0 && num < IN_MAX_DEVS)
			input_dev_map[num] = in_config_parse_dev(val);
		else
			lprintf("config: failed to parse: %s\n", var);
		return;
	}

	// key binds
	if (strncasecmp(var, "bind", 4) == 0) {
		const char *p = var + 4;
		int num = get_numvar_num(p);
		if (num < 0 || num >= IN_MAX_DEVS) {
			lprintf("config: failed to parse: %s\n", var);
			return;
		}

		num = input_dev_map[num];
		if (num < 0 || num >= IN_MAX_DEVS) {
			lprintf("config: invalid device id: %s\n", var);
			return;
		}

		while (*p && *p != ' ') p++;
		while (*p && *p == ' ') p++;
		keys_parse(p, val, num);
		return;
	}

	for (me = me_list_get_first(); me != NULL; me = me_list_get_next())
	{
		char *p;

		if (!me->need_to_save)
			continue;
		if (me->name != NULL && me->name[0] != 0) {
			if (strcasecmp(var, me->name) != 0)
				continue; /* surely not this one */
			if (me->beh == MB_OPT_ONOFF) {
				tmp = strtol(val, &p, 0);
				if (*p != 0)
					goto bad_val;
				if (tmp) *(int *)me->var |=  me->mask;
				else     *(int *)me->var &= ~me->mask;
				return;
			}
			else if (me->beh == MB_OPT_RANGE) {
				tmp = strtol(val, &p, 0);
				if (*p != 0)
					goto bad_val;
				if (tmp < me->min) tmp = me->min;
				if (tmp > me->max) tmp = me->max;
				*(int *)me->var = tmp;
				return;
			}
			else if (me->beh == MB_OPT_ENUM) {
				const char **names, *p1;
				int i;

				names = (const char **)me->data;
				if (names == NULL)
					goto bad_val;
				for (i = 0; names[i] != NULL; i++) {
					for (p1 = names[i]; *p1 == ' '; p1++)
						;
					if (strcasecmp(p1, val) == 0) {
						*(int *)me->var = i;
						return;
					}
				}
				goto bad_val;
			}
		}
		if (!custom_read(me, var, val))
			break;
		return;
	}

	lprintf("config_readsect: unhandled var: \"%s\"\n", var);
	return;

bad_val:
	lprintf("config_readsect: unhandled val for \"%s\": %s\n", var, val);
}


int config_havesect(const char *fname, const char *section)
{
	FILE *f;
	int ret;

	f = fopen(fname, "r");
	if (f == NULL) return 0;

	ret = seek_sect(f, section);
	fclose(f);
	return ret;
}

int config_readsect(const char *fname, const char *section)
{
	char line[128], *var, *val;
	FILE *f;
	int ret;

	f = fopen(fname, "r");
	if (f == NULL) return -1;

	if (section != NULL)
	{
		ret = seek_sect(f, section);
		if (!ret) {
			lprintf("config_readsect: %s: missing section [%s]\n", fname, section);
			fclose(f);
			return -1;
		}
	}

	keys_encountered = 0;
	memset(input_dev_map, 0xff, sizeof(input_dev_map));

	in_config_start();
	while (!feof(f))
	{
		ret = config_get_var_val(f, line, sizeof(line), &var, &val);
		if (ret ==  0) break;
		if (ret == -1) continue;

		parse(var, val);
	}
	in_config_end();

	fclose(f);
	return 0;
}

#endif // _MSC_VER

static char *mystrip(char *str)
{
	int i, len;

	len = strlen(str);
	for (i = 0; i < len; i++)
		if (str[i] != ' ') break;
	if (i > 0) memmove(str, str + i, len - i + 1);

	len = strlen(str);
	for (i = len - 1; i >= 0; i--)
		if (str[i] != ' ') break;
	str[i+1] = 0;

	return str;
}

/* returns:
 *  0 - EOF, end
 *  1 - parsed ok
 * -1 - failed to parse line
 */
int config_get_var_val(void *file, char *line, int lsize, char **rvar, char **rval)
{
	char *var, *val, *tmp;
	FILE *f = file;
	int len, i;

	tmp = fgets(line, lsize, f);
	if (tmp == NULL) return 0;

	if (line[0] == '[') return 0; // other section

	// strip comments, linefeed, spaces..
	len = strlen(line);
	for (i = 0; i < len; i++)
		if (line[i] == '#' || line[i] == '\r' || line[i] == '\n') { line[i] = 0; break; }
	mystrip(line);
	len = strlen(line);
	if (len <= 0) return -1;;

	// get var and val
	for (i = 0; i < len; i++)
		if (line[i] == '=') break;
	if (i >= len || strchr(&line[i+1], '=') != NULL) {
		lprintf("config_readsect: can't parse: %s\n", line);
		return -1;
	}
	line[i] = 0;
	var = line;
	val = &line[i+1];
	mystrip(var);
	mystrip(val);

#ifndef _MSC_VER
	if (strlen(var) == 0 || (strlen(val) == 0 && strncasecmp(var, "bind", 4) != 0)) {
		lprintf("config_readsect: something's empty: \"%s\" = \"%s\"\n", var, val);
		return -1;;
	}
#endif

	*rvar = var;
	*rval = val;
	return 1;
}

