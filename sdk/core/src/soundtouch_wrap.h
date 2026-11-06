#pragma once

#include <stdint.h>

void* soundtouch_create();

int soundtouch_translate(void* handle, short* data, float tempo,
	int len, int bytes_per_sample, int n_channel, int n_sampleRate,
	short* output, int output_capacity_samples);

void soundtouch_clear_samples(void* handle);

void soundtouch_destroy(void* handle);


