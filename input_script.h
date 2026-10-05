#ifndef INPUT_SCRIPT_H
#define INPUT_SCRIPT_H

#include <stdint.h>

// Scripted input: a text file of lines "<f0> <f1> <key>", meaning hold <key>
// from guest frame f0 to f1 inclusive. <key> is one of:
//   coin start select a b c d up down left right
// Lines starting with '#' are comments. The PC headless harness reads it
// from MVS64_INPUT; an N64 build made with INPUT=<file> replays it from
// the DFS (MVS64_AUTOINPUT). Frame-keyed, so both builds see the same
// input at the same guest frame regardless of speed.

// Load a script; returns the number of events (0 if the file is missing).
int input_script_load(const char *path);

// Set keys[k] = 1 for every key the script holds at this guest frame.
// keys is indexed by PLAT_KEY_* codes; other entries are left alone.
void input_script_keys(int frame, uint8_t *keys);

#endif
