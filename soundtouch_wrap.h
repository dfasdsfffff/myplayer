#ifndef IJKSOUNDTOUCHWRAP_H
#define IJKSOUNDTOUCHWRAP_H

#include <stdint.h>

void* soundtouch_create();

int soundtouch_translate(void* handle, short* data, float speed, float pitch,
	int len, int bytes_per_sample, int n_channel, int n_sampleRate);

void soundtouch_destroy(void* handle);

#endif /* IJKSOUNDTOUCHWRAP_H */
