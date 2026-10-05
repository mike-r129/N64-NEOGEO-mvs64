#include <stdio.h>
#include <string.h>
#include "input_script.h"
#include "platform.h"

#define IS_MAX_EVENTS 256
static struct { int f0, f1, key; } is_events[IS_MAX_EVENTS];
static int is_nevents;

static int is_keyname(const char *n) {
	if (!strcmp(n, "coin"))   return PLAT_KEY_COIN_1;
	if (!strcmp(n, "start"))  return PLAT_KEY_P1_START;
	if (!strcmp(n, "select")) return PLAT_KEY_P1_SELECT;
	if (!strcmp(n, "a"))      return PLAT_KEY_P1_A;
	if (!strcmp(n, "b"))      return PLAT_KEY_P1_B;
	if (!strcmp(n, "c"))      return PLAT_KEY_P1_C;
	if (!strcmp(n, "d"))      return PLAT_KEY_P1_D;
	if (!strcmp(n, "up"))     return PLAT_KEY_P1_UP;
	if (!strcmp(n, "down"))   return PLAT_KEY_P1_DOWN;
	if (!strcmp(n, "left"))   return PLAT_KEY_P1_LEFT;
	if (!strcmp(n, "right"))  return PLAT_KEY_P1_RIGHT;
	return -1;
}

int input_script_load(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) { fprintf(stderr, "[INPUT] cannot open %s\n", path); return 0; }
	char line[128], key[32];
	int f0, f1;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
		if (sscanf(line, "%d %d %31s", &f0, &f1, key) == 3) {
			int k = is_keyname(key);
			if (k < 0) { fprintf(stderr, "[INPUT] bad key '%s'\n", key); continue; }
			if (is_nevents < IS_MAX_EVENTS)
				is_events[is_nevents++] = (typeof(is_events[0])){ f0, f1, k };
		}
	}
	fclose(f);
	fprintf(stderr, "[INPUT] loaded %d events from %s\n", is_nevents, path);
	return is_nevents;
}

void input_script_keys(int frame, uint8_t *keys) {
	for (int i = 0; i < is_nevents; i++)
		if (frame >= is_events[i].f0 && frame <= is_events[i].f1)
			keys[is_events[i].key] = 1;
}
