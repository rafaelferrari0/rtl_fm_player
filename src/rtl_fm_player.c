/*
 * rtl_fm_player, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Based on rtl_fm_streamer by Albrecht Lohoefener
 * Based on "rtl_fm", see http://sdr.osmocom.org/trac/wiki/rtl-sdr for details
 *
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012 by Hoernchen <la@tfc-server.de>
 * Copyright (C) 2012 by Kyle Keen <keenerd@gmail.com>
 * Copyright (C) 2013 by Elias Oenal <EliasOenal@gmail.com>
 * Copyright (C) 2015 by Miroslav Slugen <thunder.m@email.cz>
 * Copyright (C) 2015 by Albrecht Lohoefener <albrechtloh@gmx.de>
 * Copyright (C) 2020 by Rafael Ferrari <rafaelbf@hotmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#else
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include "getopt/getopt.h"
#define msleep(x) Sleep(x)
#define usleep(x) Sleep(x/1000)
#ifdef _MSC_VER
#define round(x) (x > 0.0 ? floor(x + 0.5): ceil(x - 0.5))
#endif
#define _USE_MATH_DEFINES
#endif

#include <math.h>
#include <pthread.h>
#include <libusb.h>
#include <libzplay.h>

#include "rtl-sdr.h"
#include "convenience/convenience.h"

#define VERSION "0.0.1"

#define DEFAULT_SAMPLE_RATE		240000
#define DEFAULT_BUF_LENGTH		(1 * 16384)
#define MAXIMUM_OVERSAMPLE		16
#define MAXIMUM_BUF_LENGTH		(MAXIMUM_OVERSAMPLE * DEFAULT_BUF_LENGTH)
#define AUTO_GAIN				100
#define BUFFER_DUMP				4096

#define FREQUENCIES_LIMIT		1000

#define PI2_F           6.28318531f
#define PI_F            3.14159265f
#define PI_2_F          1.5707963f
#define PI_4_F          0.78539816f

#define DEEMPHASIS_NONE         0
#define DEEMPHASIS_FM_EU        0.000050
#define DEEMPHASIS_FM_USA       0.000075


typedef enum
{
    false = 0,
    true
}bool;


// circular buffer for timeshift
char * circbuffer;
int circbuffeshift;
int circbufferslots;

static volatile int do_exit = 0;
static int lcm_post[17] =
{ 1, 1, 1, 3, 1, 5, 3, 7, 1, 9, 5, 11, 3, 13, 7, 15, 1 };
static int ACTUAL_BUF_LENGTH;

/* 8 MB */
static char input_buffer[16 * MAXIMUM_BUF_LENGTH];
static char output_buffer[16 * MAXIMUM_BUF_LENGTH];
uint32_t input_buffer_rpos = 0,
         input_buffer_wpos = 0,
         input_buffer_size = 0,
         input_buffer_size_max = 16 * MAXIMUM_BUF_LENGTH;
uint32_t output_buffer_rpos = 0,
         output_buffer_wpos = 0,
         output_buffer_size = 0,
         output_buffer_size_max = 16 * MAXIMUM_BUF_LENGTH;

// win32 libzplay dll
ZPLAY_HANDLE libzplay;

//static int ConnectionDesc;
bool isStartStream;
//bool isReading;

struct lp_real
{
	float *br;
	float *bm;
	float *bs;
	float *fm;
	float *fp;
	float *fs;
	float swf;
	float cwf;
	float pp;
	int pos;
	int size;
	int rsize;
	int mode;
};

struct dongle_state
{
	int exit_flag;
	pthread_t thread;
	rtlsdr_dev_t *dev;
	int dev_index;
	uint32_t freq;
	uint32_t rate;
	int gain;
	int ppm_error;
	int direct_sampling;
	int mute;
	struct demod_state *demod_target;
};

struct demod_state
{
	int exit_flag;
	pthread_t thread;
	uint8_t buf[MAXIMUM_BUF_LENGTH];
	uint32_t buf_len;
	/* required 4 bytes for F32 part */
	int16_t lowpassed[MAXIMUM_BUF_LENGTH << 1];
	int lp_len;
	/* tmp buffer for low pass filter F32 */
	float lowpass_tb[48];
	int16_t lp_i_hist[10][6];
	int16_t lp_q_hist[10][6];
	/* result buffer fo FM will be always 1/2 of lowpassed or less, so no need to shift */
	int16_t result[MAXIMUM_BUF_LENGTH];
	int result_len;
	int16_t droop_i_hist[9];
	int16_t droop_q_hist[9];
	int offset_tuning;
	int rate_in;
	int rate_out;
	int rate_out2;
	int now_r, now_j;
	int pre_r, pre_j;
	float pre_r_f32, pre_j_f32;
	int prev_index;
	int downsample; /* min 1, max 256 */
	int post_downsample;
	int output_scale;
	int squelch_level, conseq_squelch, squelch_hits, terminate_on_squelch;
	int downsample_passes;
	int comp_fir_size;
	int custom_atan;
	double deemph;
	int deemph_a;
	int deemph_l;
	int deemph_r;
	float deemph_l_f32;
	float deemph_r_f32;
	float deemph_lambda;
	float volume;
	int now_lpr;
	int prev_lpr_index;
	struct lp_real lpr;
	pthread_rwlock_t rw;
	pthread_cond_t ready;
	pthread_mutex_t ready_m;
	struct output_state *output_target;
};

struct output_state
{
	int exit_flag;
	pthread_t thread;
//	FILE *file;
	char *filename;
	int rate;
	int16_t *result;
	int result_len;
	pthread_rwlock_t rw;
	pthread_cond_t ready;
	pthread_mutex_t ready_m;
};

struct controller_state
{
	int exit_flag;
	pthread_t thread;
	uint32_t freqs[FREQUENCIES_LIMIT];
	int freq_len;
	int freq_now;
	int edge;
	int wb_mode;
	pthread_cond_t hop;
	pthread_mutex_t hop_m;
};


float RMSShadowBuf[MAXIMUM_BUF_LENGTH << 1];
int RMSShadowBuf_len;


// multiple of these, eventually
struct dongle_state dongle;
struct demod_state demod;
struct output_state output;
struct controller_state controller;


  static const char WAVHeaderStereo[] = {
	0x52, 0x49, 0x46, 0x46, 0x24, 0xEE, 0x02, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20, 
	0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x80, 0xBB, 0x00, 0x00, 0x00, 0xEE, 0x02, 0x00, 
	0x04, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0xEE, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00};
    

  static const char WAVHeaderMono[] = {
	0x52, 0x49, 0x46, 0x46, 0x24, 0x77, 0x01, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20, 
	0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x80, 0xBB, 0x00, 0x00, 0x00, 0x77, 0x01, 0x00, 
	0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00};    


void usage(void)
{
	fprintf(
	stderr, "rtl_fm_player, a simple narrow band FM demodulator for RTL2832 based DVB-T receivers\n\n"
			"Use:\trtl_fm_player -f freq [-options] [filename]\n"
			"\t[-M modulation (default: wbfm)]\n"
			"\t    fm, wbfm, raw, am, usb, lsb\n"
			"\t    wbfm == -M fm -s 170k -o 4 -A fast -r 32k -l 0 -E deemp\n"
			"\t    raw mode outputs 2x16 bit IQ pairs\n"
			"\t[-s sample_rate (default: 24k)]\n"
			"\t[-d device_index (default: 0)]\n"
		  "\t[-T enable bias-T on GPIO PIN 0 (works for rtl-sdr.com v3 dongles)]\n"
			"\t[-g tuner_gain (default: automatic)]\n"
			"\t[-l squelch_level (default: 0/off)]\n"
			"\t[-p ppm_error (default: 0)]\n"
			"\t[-E enable_option (default: none)]\n"
			"\t    use multiple -E to enable multiple options\n"
			"\t    edge:   enable lower edge tuning\n"
			"\t    dc:     enable dc blocking filter\n"
			"\t    deemp:  enable de-emphasis filter\n"
			"\t    direct: enable direct sampling\n"
			"\t    offset: enable offset tuning\n"
			"\tfilename (supported file extensions .aac .flac .mp3 .ogg .wav)\n"
			"\t[-X Start with FM Stereo support]\n"
			"\t[-Y Start with FM Mono support]\n"
			"\n"
			"Experimental options:\n"
			"\t[-r resample_rate (default: 48000)]\n"
			"\t[-t squelch_delay (default: 10)]\n"
			"\t    +values will mute/scan, -values will exit\n"
			"\t[-F fir_size (default: off)]\n"
			"\t    enables low-leakage downsample filter\n"
			"\t    size can be 0 or 9.  0 has bad roll off\n"
			"\t[-A std/fast/lut choose atan math (default: std)]\n"
			"\n");
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI sighandler(int signum)
{
	if (CTRL_C_EVENT == signum)
	{
		fprintf(stderr, "\nSignal caught, exiting!\n");
		do_exit = 1;
    fflush(stdin);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	fprintf(stderr, "\nSignal caught, exiting!\n");
	do_exit = 1;
  fflush(stdin); 
}
#endif

/* more cond dumbness */
#define safe_cond_signal(n, m) pthread_mutex_lock(m); pthread_cond_signal(n); pthread_mutex_unlock(m)
#define safe_cond_wait(n, m) pthread_mutex_lock(m); pthread_cond_wait(n, m); pthread_mutex_unlock(m)

/* {length, coef, coef, coef}  and scaled by 2^15
 for now, only length 9, optimal way to get +85% bandwidth */
#define CIC_TABLE_MAX 10
int cic_9_tables[][10] =
{
{ 0, },
{ 9, -156, -97, 2798, -15489, 61019, -15489, 2798, -97, -156 },
{ 9, -128, -568, 5593, -24125, 74126, -24125, 5593, -568, -128 },
{ 9, -129, -639, 6187, -26281, 77511, -26281, 6187, -639, -129 },
{ 9, -122, -612, 6082, -26353, 77818, -26353, 6082, -612, -122 },
{ 9, -120, -602, 6015, -26269, 77757, -26269, 6015, -602, -120 },
{ 9, -120, -582, 5951, -26128, 77542, -26128, 5951, -582, -120 },
{ 9, -119, -580, 5931, -26094, 77505, -26094, 5931, -580, -119 },
{ 9, -119, -578, 5921, -26077, 77484, -26077, 5921, -578, -119 },
{ 9, -119, -577, 5917, -26067, 77473, -26067, 5917, -577, -119 },
{ 9, -199, -362, 5303, -25505, 77489, -25505, 5303, -362, -199 }, };

/* table for u8 -> f32 conversion, 0 = positive, 1 = negative */
static float u8_f32_table[2][256] =
{
{ 0 },
{ 0 } };

/* table with low pass filter coeficients */
static float lp_filter_f32[16] =
{ 0 };

#ifdef _MSC_VER
double log2(double n)
{
	return log(n) / log(2.0);
}
#endif

void init_u8_f32_table()
{
	int i;

	for (i = 0; i < 256; i++)
	{
		u8_f32_table[0][i] = ((float) i - 127.5f) / 128.0f;
		u8_f32_table[1][i] = ((float) i - 127.5f) / -128.0f;
	}
}

void rotate_90_u8_f32(struct demod_state *d)
/* 90 rotation is 1+0j, 0+1j, -1+0j, 0-1j
 or [0, 1, -3, 2, -4, -5, 7, -6] */
{
	float *ob = (float*) d->lowpassed;
	uint32_t i;

	for (i = 0; i < d->buf_len; i += 8)
	{
		ob[i] = u8_f32_table[0][d->buf[i]];
		ob[i + 1] = u8_f32_table[0][d->buf[i + 1]];
		ob[i + 2] = u8_f32_table[1][d->buf[i + 3]];
		ob[i + 3] = u8_f32_table[0][d->buf[i + 2]];
		ob[i + 4] = u8_f32_table[1][d->buf[i + 4]];
		ob[i + 5] = u8_f32_table[1][d->buf[i + 5]];
		ob[i + 6] = u8_f32_table[0][d->buf[i + 7]];
		ob[i + 7] = u8_f32_table[1][d->buf[i + 6]];
	}

	d->lp_len = d->buf_len;
}

void u8_f32(struct demod_state *d)
{
	float *ob = (float*) d->lowpassed;
	uint32_t i;

	for (i = 0; i < d->buf_len; i++)
	{
		ob[i] = u8_f32_table[0][d->buf[i]];
	}

	d->lp_len = d->buf_len;
}

void init_lp_f32()
{
	int i;
	float j;

	for (i = 0; i < 16; i++)
	{
		j = (float) i - 15.5f;
		lp_filter_f32[i] = (sinf(0.125f * PI_F * j) / (PI_F * j)) * (0.54f - 0.46f * cosf(PI_F * (float) i / 15.5f));
	}
}

void lp_f32(struct demod_state *d)
{
	int i, j;
	float xb[6] =
	{ 0 }, *ob = (float*) d->lowpassed, *tb = d->lowpass_tb, *fb = lp_filter_f32;

    /* first three reads needs data from tmp buffer */
    /* I - 0 */
    xb[0] = (tb[ 0] + ob[14]) * fb[0] +
            (tb[ 2] + ob[12]) * fb[1] +
            (tb[ 4] + ob[10]) * fb[2] +
            (tb[ 6] + ob[ 8]) * fb[3] +
            (tb[ 8] + ob[ 6]) * fb[4] +
            (tb[10] + ob[ 4]) * fb[5] +
            (tb[12] + ob[ 2]) * fb[6] +
            (tb[14] + ob[ 0]) * fb[7] +
            (tb[16] + tb[46]) * fb[8] +
            (tb[18] + tb[44]) * fb[9] +
            (tb[20] + tb[42]) * fb[10] +
            (tb[22] + tb[40]) * fb[11] +
            (tb[24] + tb[38]) * fb[12] +
            (tb[26] + tb[36]) * fb[13] +
            (tb[28] + tb[34]) * fb[14] +
            (tb[30] + tb[32]) * fb[15];
    /* Q - 0 */
    xb[1] = (tb[ 1] + ob[15]) * fb[0] +
            (tb[ 3] + ob[13]) * fb[1] +
            (tb[ 5] + ob[11]) * fb[2] +
            (tb[ 7] + ob[ 9]) * fb[3] +
            (tb[ 9] + ob[ 7]) * fb[4] +
            (tb[11] + ob[ 5]) * fb[5] +
            (tb[13] + ob[ 3]) * fb[6] +
            (tb[15] + ob[ 1]) * fb[7] +
            (tb[17] + tb[47]) * fb[8] +
            (tb[19] + tb[45]) * fb[9] +
            (tb[21] + tb[43]) * fb[10] +
            (tb[23] + tb[41]) * fb[11] +
            (tb[25] + tb[39]) * fb[12] +
            (tb[27] + tb[37]) * fb[13] +
            (tb[29] + tb[35]) * fb[14] +
            (tb[31] + tb[33]) * fb[15];

    /* I - 1 */
    xb[2] = (tb[16] + ob[30]) * fb[0] +
            (tb[18] + ob[28]) * fb[1] +
            (tb[20] + ob[26]) * fb[2] +
            (tb[22] + ob[24]) * fb[3] +
            (tb[24] + ob[22]) * fb[4] +
            (tb[26] + ob[20]) * fb[5] +
            (tb[28] + ob[18]) * fb[6] +
            (tb[30] + ob[16]) * fb[7] +
            (tb[32] + ob[14]) * fb[8] +
            (tb[34] + ob[12]) * fb[9] +
            (tb[36] + ob[10]) * fb[10] +
            (tb[38] + ob[ 8]) * fb[11] +
            (tb[40] + ob[ 6]) * fb[12] +
            (tb[42] + ob[ 4]) * fb[13] +
            (tb[44] + ob[ 2]) * fb[14] +
            (tb[46] + ob[ 0]) * fb[15];
    /* Q - 1 */
    xb[3] = (tb[17] + ob[31]) * fb[0] +
            (tb[19] + ob[29]) * fb[1] +
            (tb[21] + ob[27]) * fb[2] +
            (tb[23] + ob[25]) * fb[3] +
            (tb[25] + ob[23]) * fb[4] +
            (tb[27] + ob[21]) * fb[5] +
            (tb[29] + ob[19]) * fb[6] +
            (tb[31] + ob[17]) * fb[7] +
            (tb[33] + ob[15]) * fb[8] +
            (tb[35] + ob[13]) * fb[9] +
            (tb[37] + ob[11]) * fb[10] +
            (tb[39] + ob[ 9]) * fb[11] +
            (tb[41] + ob[ 7]) * fb[12] +
            (tb[43] + ob[ 5]) * fb[13] +
            (tb[45] + ob[ 3]) * fb[14] +
            (tb[47] + ob[ 1]) * fb[15];

    /* I - 2 */
    xb[4] = (tb[32] + ob[46]) * fb[0] +
            (tb[34] + ob[44]) * fb[1] +
            (tb[36] + ob[42]) * fb[2] +
            (tb[38] + ob[40]) * fb[3] +
            (tb[40] + ob[38]) * fb[4] +
            (tb[42] + ob[36]) * fb[5] +
            (tb[44] + ob[34]) * fb[6] +
            (tb[46] + ob[32]) * fb[7] +
            (ob[ 0] + ob[30]) * fb[8] +
            (ob[ 2] + ob[28]) * fb[9] +
            (ob[ 4] + ob[26]) * fb[10] +
            (ob[ 6] + ob[24]) * fb[11] +
            (ob[ 8] + ob[22]) * fb[12] +
            (ob[10] + ob[20]) * fb[13] +
            (ob[12] + ob[18]) * fb[14] +
            (ob[14] + ob[16]) * fb[15];
    /* Q - 2 */
    xb[5] = (tb[33] + ob[47]) * fb[0] +
            (tb[35] + ob[45]) * fb[1] +
            (tb[37] + ob[43]) * fb[2] +
            (tb[39] + ob[41]) * fb[3] +
            (tb[41] + ob[39]) * fb[4] +
            (tb[43] + ob[37]) * fb[5] +
            (tb[45] + ob[35]) * fb[6] +
            (tb[47] + ob[33]) * fb[7] +
            (ob[ 1] + ob[31]) * fb[8] +
            (ob[ 3] + ob[29]) * fb[9] +
            (ob[ 5] + ob[27]) * fb[10] +
            (ob[ 7] + ob[25]) * fb[11] +
            (ob[ 9] + ob[23]) * fb[12] +
            (ob[11] + ob[21]) * fb[13] +
            (ob[13] + ob[19]) * fb[14] +
            (ob[15] + ob[17]) * fb[15];

	/* store last 24 IQ values for next read */
	memcpy(tb, &(ob[d->lp_len - 48]), 192);

    /* next reads are direct */
    for (i = 0, j = 6; j < d->lp_len >> 3; i+= 16, j+= 2) {
        /* I */
        ob[j] = (ob[i     ] + ob[i + 62]) * fb[0] +
                (ob[i +  2] + ob[i + 60]) * fb[1] +
                (ob[i +  4] + ob[i + 58]) * fb[2] +
                (ob[i +  6] + ob[i + 56]) * fb[3] +
                (ob[i +  8] + ob[i + 54]) * fb[4] +
                (ob[i + 10] + ob[i + 52]) * fb[5] +
                (ob[i + 12] + ob[i + 50]) * fb[6] +
                (ob[i + 14] + ob[i + 48]) * fb[7] +
                (ob[i + 16] + ob[i + 46]) * fb[8] +
                (ob[i + 18] + ob[i + 44]) * fb[9] +
                (ob[i + 20] + ob[i + 42]) * fb[10] +
                (ob[i + 22] + ob[i + 40]) * fb[11] +
                (ob[i + 24] + ob[i + 38]) * fb[12] +
                (ob[i + 26] + ob[i + 36]) * fb[13] +
                (ob[i + 28] + ob[i + 34]) * fb[14] +
                (ob[i + 30] + ob[i + 32]) * fb[15];
        /* Q */
        ob[j + 1] = (ob[i + 1] + ob[i + 63]) * fb[0] +
                (ob[i +  3] + ob[i + 61]) * fb[1] +
                (ob[i +  5] + ob[i + 59]) * fb[2] +
                (ob[i +  7] + ob[i + 57]) * fb[3] +
                (ob[i +  9] + ob[i + 55]) * fb[4] +
                (ob[i + 11] + ob[i + 53]) * fb[5] +
                (ob[i + 13] + ob[i + 51]) * fb[6] +
                (ob[i + 15] + ob[i + 49]) * fb[7] +
                (ob[i + 17] + ob[i + 47]) * fb[8] +
                (ob[i + 19] + ob[i + 45]) * fb[9] +
                (ob[i + 21] + ob[i + 43]) * fb[10] +
                (ob[i + 23] + ob[i + 41]) * fb[11] +
                (ob[i + 25] + ob[i + 39]) * fb[12] +
                (ob[i + 27] + ob[i + 37]) * fb[13] +
                (ob[i + 29] + ob[i + 35]) * fb[14] +
                (ob[i + 31] + ob[i + 33]) * fb[15];
    }

	/* copy data back to output buffer after encoding */
	memcpy(ob, xb, 24);

	/* output has always fixed size */
	d->lp_len >>= 3;
}

void init_lp_real_f32(struct demod_state *fm)
{
	int i;
	float fmh, fpl, fph, fsl, fsh, fv, fi, fh, wf;

	fprintf(stderr, "Init FIR hamming, size: %d sample_rate: %d\n", fm->lpr.size, fm->rate_in);
	fm->lpr.rsize = (fm->lpr.size >> 1);
	wf = PI2_F * 19000.0f / (float) fm->rate_in;
	fm->lpr.swf = sinf(wf);
	fm->lpr.cwf = cosf(wf);
	fm->lpr.pp = 0;
	fmh = 16000.0f / (float) fm->rate_in;
	fpl = 18000.0f / (float) fm->rate_in;
	fph = 20000.0f / (float) fm->rate_in;
	fsl = 21000.0f / (float) fm->rate_in;
	fsh = 55000.0f / (float) fm->rate_in;
	fm->lpr.br = calloc(fm->lpr.size, 4);
	fm->lpr.bm = calloc(fm->lpr.size, 4);
	fm->lpr.bs = calloc(fm->lpr.size, 4);
	/* filters are symetrical, so only half size */
	fm->lpr.fm = calloc(fm->lpr.size >> 1, 4);
	fm->lpr.fp = calloc(fm->lpr.size >> 1, 4);
	fm->lpr.fs = calloc(fm->lpr.size >> 1, 4);
	fm->lpr.pos = 0;
	for (i = 0; i < fm->lpr.rsize; i++)
	{
		fi = (float) i - (float) (fm->lpr.size - 1) / 2.0f;
		/* hamming window */
		fh = 0.54f - 0.46f * cosf(PI2_F * (float) i / (float) (fm->lpr.size - 1));
		/* low pass */
		fv = (fi == 0) ? 2.0f * fmh : sinf(PI2_F * fmh * fi) / (PI_F * fi);
		fm->lpr.fm[i] = fv * fh;
		/* pilot band pass */
		fv = (fi == 0) ? 2.0f * (fph - fpl) : (sinf(PI2_F * fph * fi) - sinf(PI2_F * fpl * fi)) / (PI_F * fi);
		fm->lpr.fp[i] = fv * fh;
		/* stereo band pass */
		fv = (fi == 0) ? 2.0f * (fsh - fsl) : (sinf(PI2_F * fsh * fi) - sinf(PI2_F * fsl * fi)) / (PI_F * fi);
		fm->lpr.fs[i] = fv * fh;
	}
}

void deinit_lp_real_f32(struct demod_state *fm)
{
	fm->lpr.rsize = 0;
	free(fm->lpr.br);
	free(fm->lpr.bm);
	free(fm->lpr.bs);
	free(fm->lpr.fm);
	free(fm->lpr.fp);
	free(fm->lpr.fs);
	fm->lpr.br = NULL;
	fm->lpr.bm = NULL;
	fm->lpr.bs = NULL;
	fm->lpr.fm = NULL;
	fm->lpr.fp = NULL;
	fm->lpr.fs = NULL;
}

float sin2atan2_f32(float x, float y)
{
	float z;

	if (x == 0.f) return 0.f;

	z = y / x;

	return (z + z) / (1.f + (z * z));
}

void lp_real_f32(struct demod_state *fm)
{
	int i, j, k, l, o = 0, fast = (int) fm->rate_out, slow = (int) fm->rate_out2;
	float v, vm, vp, vs, *ib = (float*) fm->result;

	switch (fm->lpr.mode)
	{
	case 0:
		for (i = 0; i < fm->result_len; i++)
		{
			if ((fm->prev_lpr_index += slow) >= fast)
			{
				fm->prev_lpr_index -= fast;
				ib[o++] = ib[i];
			}
		}
		break;
	case 1: // Mono
		for (i = 0; i < fm->result_len; i++)
		{
			fm->lpr.br[fm->lpr.pos] = ib[i];

			if (++fm->lpr.pos == fm->lpr.size) fm->lpr.pos = 0;

			if ((fm->prev_lpr_index += slow) >= fast)
			{
				fm->prev_lpr_index -= fast;

				for (j = fm->lpr.pos, k = 0, l = fm->lpr.pos, vm = 0; k < fm->lpr.rsize; k++)
				{
					/* next value before storing, easiest way to get complementary index */
					if (l == 0)
					{
						l = fm->lpr.size - 1;
					}
					else
					{
						l--;
					}

					vm += (fm->lpr.br[j] + fm->lpr.br[l]) * fm->lpr.fm[k];

					/* next value after storing */
					if (++j == fm->lpr.size) j = 0;
				}

				ib[o++] = vm;
			}
		}
		break;
	case 2: // Stereo
		for (i = 0; i < fm->result_len; i++)
		{
			fm->lpr.br[fm->lpr.pos] = ib[i];

			for (j = fm->lpr.pos, k = 0, l = fm->lpr.pos, vm = 0, vp = 0, vs = 0; k < fm->lpr.rsize; k++)
			{
				/* next value after storing */
				if (++j == fm->lpr.size) j = 0;

				v = fm->lpr.br[j] + fm->lpr.br[l];

				vm += v * fm->lpr.fm[k]; // L+R low pass (0 Hz ... 17 kHz)
				vp += v * fm->lpr.fp[k]; // Pilot frequency band pass (18 kHz ... 20 kHz) --> filters out the 19 kHz
				vs += v * fm->lpr.fs[k]; // L-R band pass (21 kHz ... 55 kHz)

				/* next value before storing, easiest way to get complementary index */
				if (l == 0)
				{
					l = fm->lpr.size - 1;
				}
				else
				{
					l--;
				}
			}

			fm->lpr.bm[fm->lpr.pos] = vm;

			// AM L-R demodulation
			// sin2atan2f(...) doubles the pilot frequency 19 kHz --> 38 kHz
			// vs * sin2atan2_f32(...) AM demodulation
			fm->lpr.bs[fm->lpr.pos] = vs * sin2atan2_f32(vp * fm->lpr.swf, vp * fm->lpr.cwf - fm->lpr.pp);
			fm->lpr.pp = vp;

			if (++fm->lpr.pos == fm->lpr.size) fm->lpr.pos = 0;

			if ((fm->prev_lpr_index += slow) >= fast)
			{
				fm->prev_lpr_index -= fast;

				for (j = fm->lpr.pos, k = 0, l = fm->lpr.pos, vm = 0, vs = 0; k < fm->lpr.rsize; k++)
				{
					/* next value before storing, easiest way to get complementary index */
					if (l == 0)
					{
						l = fm->lpr.size - 1;
					}
					else
					{
						l--;
					}

					vm += (fm->lpr.bm[j] + fm->lpr.bm[l]) * fm->lpr.fm[k]; // low pass (0 Hz ... 17 kHz)
					vs += (fm->lpr.bs[j] + fm->lpr.bs[l]) * fm->lpr.fm[k]; // low pass (0 Hz ... 17 kHz), removes unwanted AM demodulation high frequencies

					/* next value after storing */
					if (++j == fm->lpr.size) j = 0;
				}

				/* we can overwrite input, but not for downsample input buffer 16384 */
				// Calculate stereo signal
				ib[o] = vm + vs;
				ib[o + 1] = vm - vs;
				o += 2;
			}
		}
		break;
	}

	fm->result_len = o;
}

/* absolute error < 0.0015, computation error: 0.012%, mono: 0.010, stereo: 0.007% */
static float atan2_lagrange_f32(float y, float x)
{
	float z;

	if (x == 0.f)
	{
		if (y < 0.f) return -PI_2_F;
		if (y > 0.f) return PI_2_F;
		return 0.f;
	}

	if (y == 0.f) return (x < 0.f) ? PI_F : 0.f;

	/* Q3 + Q2 */
	if (x < 0.f)
	{
		/* Q3 + */
		if (y < 0.f)
		{
			/* abs(x) >= abs(y) */
			if (x <= y)
			{
				z = y / x;
				return z * (PI_4_F - (z - 1.f) * (0.2447f + 0.0663f * z)) - PI_F;
			}
			/* abs(x) < abs(y) */
			z = x / y;
			return z * (-PI_4_F + (z - 1.f) * (0.2447f + 0.0663f * z)) - PI_2_F;
		}
		/* Q2 - */
		if (-x >= y)
		{
			z = y / x;
			return z * (PI_4_F + (z + 1.f) * (0.2447f - 0.0663f * z)) + PI_F;
		}
		z = x / y;
		return PI_2_F - z * (PI_4_F + (z + 1.f) * (0.2447f - 0.0663f * z));
	}

	/* Q4 - */
	if (y < 0.f)
	{
		if (x >= -y)
		{
			z = y / x;
			return z * (PI_4_F + (z + 1.f) * (0.2447f - 0.0663f * z));
		}
		z = x / y;
		return z * (-PI_4_F - (z + 1.f) * (0.2447f - 0.0663f * z)) - PI_2_F;
	}

	/* Q1 + */
	if (x >= y)
	{
		z = y / x;
		return z * (PI_4_F - (z - 1.f) * (0.2447f + 0.0663f * z));
	}

	z = x / y;
	return PI_2_F - z * (PI_4_F - (z - 1.f) * (0.2447f + 0.0663f * z));
}

void fm_demod_f32(struct demod_state *fm)
{
	int i;
	float *ib = (float*) fm->lowpassed, *ob = (float*) fm->result, v;

	fm->result_len = 0;

	for (i = 0; i < fm->lp_len; i += 2)
	{
		/* atanf function needs more computer power, better is to use approximation */
		v = atan2_lagrange_f32(fm->pre_r_f32 * ib[i + 1] - fm->pre_j_f32 * ib[i],
				ib[i] * fm->pre_r_f32 + ib[i + 1] * fm->pre_j_f32);
		fm->pre_r_f32 = ib[i];
		fm->pre_j_f32 = ib[i + 1];
		ob[fm->result_len++] = v;
	}
}

void deemph_filter_f32(struct demod_state *fm)
{
	int i;
	float *ib = (float*) fm->result;

	if (fm->lpr.mode == 2)
	{
		for (i = 0; i < fm->result_len; i += 2)
		{
			/* left */
			fm->deemph_l_f32 = (ib[i] += fm->deemph_lambda * (fm->deemph_l_f32 - ib[i]));
			/* right */
			fm->deemph_r_f32 = (ib[i + 1] += fm->deemph_lambda * (fm->deemph_r_f32 - ib[i + 1]));
		}
	}
	else
	{
		for (i = 0; i < fm->result_len; i++)
		{
			fm->deemph_l_f32 = (ib[i] += fm->deemph_lambda * (fm->deemph_l_f32 - ib[i]));
		}
	}
}

void convert_f32_s16(struct demod_state *fm)
{
	int i;
	float *ib = (float*) fm->result, coef;
	int16_t *ob = (int16_t*) fm->result;

	coef = fm->volume * 32768.0f;

	for (i = 0; i < fm->result_len; i++)
	{
		ib[i] *= coef;
		if (ib[i] > 32767.0f)
		{
			ob[i] = 32767;
		}
		else if (ib[i] < -32768.0f)
		{
			ob[i] = -32768;
		}
		else
		{
			ob[i] = (int16_t) lrintf(ib[i]);
		}
	}
}

int rms(int16_t *samples, int len, int step)
/* largely lifted from rtl_power */
{
	int i;
	long p, t, s;
	double dc, err;

	p = t = 0L;
	for (i=0; i<len; i+=step) {
		s = (long)samples[i];
		t += s;
		p += s * s;
	}
	/* correct for dc offset in squares */
	dc = (double)(t*step) / (double)len;
	err = t * 2 * dc - dc * dc * len;

	return (int)sqrt((p-err) / len);
}


void full_demod(struct demod_state *d)
{
	int i, ds_p;
	int sr = 0;

	// Low pass to filter only to the tuned FM channel
	lp_f32(d);

	// Shadow buffer to calculate the RMS
	memcpy(RMSShadowBuf, d->lowpassed, d->lp_len * sizeof(float));
	RMSShadowBuf_len = d->lp_len;

	// FM demodulation
	fm_demod_f32(d); /* lowpassed -> result */

	/* todo, fm noise squelch */
	
	// use nicer filter here too?
	if (d->post_downsample > 1)
	{
		/* For float not implemented */
	}

	if (d->rate_out2 > 0)
		lp_real_f32(d);

	if (d->deemph)
		deemph_filter_f32(d);

	convert_f32_s16(d);
}

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	int i;
	struct dongle_state *s = ctx;
	struct demod_state *d = s->demod_target;

	if (do_exit) { 
    rtlsdr_cancel_async(dongle.dev);
    return;
  }
	if (!ctx) {
    return;
  }

	/* mute */
	if (s->mute)
	{
		for (i = 0; i < s->mute; i++)
			buf[i] = 127;
		s->mute = 0;
	}

	pthread_rwlock_wrlock(&d->rw);
	if (input_buffer_wpos + len <= input_buffer_size_max)
	{
		memcpy(input_buffer + input_buffer_wpos, buf, len);
		input_buffer_wpos += len;
		input_buffer_size += len;
		/* begin new read with zero */
		if (input_buffer_wpos == input_buffer_size_max) input_buffer_wpos = 0;
	}
	else
	{
		/* buffer_size_max must be multiple of len */
		memcpy(input_buffer, buf, len);
		input_buffer_wpos = len;
		input_buffer_size += len;
	}
	/* already droped some data, so print info */
	if (input_buffer_size > input_buffer_size_max)
	{
		fprintf(stderr, "dropping input buffer: %u B\n", input_buffer_size - input_buffer_size_max);
		input_buffer_size = input_buffer_size_max;
	}
	pthread_rwlock_unlock(&d->rw);
	//safe_cond_signal(&d->ready, &d->ready_m);
}

static void * dongle_thread_fn(void *arg)
{
  int r = 0;
	struct dongle_state *s = arg;

	if (do_exit) return 0;

	r= rtlsdr_read_async(s->dev, rtlsdr_callback, s, 0, 0);
  if (r < 0) {
    fprintf(stderr, "WARNING: async read failed.\n");
  }
  
	return 0;
}

static void * demod_thread_fn(void *arg)
{
	struct demod_state *d = arg;
	struct output_state *o = d->output_target;
	uint32_t len;

	while (!do_exit)
	{
		len = MAXIMUM_BUF_LENGTH;
		while (input_buffer_size < len)
		{
			if ((d->exit_flag) || (do_exit)) return 0;
			usleep(5000);
		}

		pthread_rwlock_wrlock(&d->rw);
		memcpy(d->buf, input_buffer + input_buffer_rpos, len);
		input_buffer_rpos += len;
		input_buffer_size -= len;
		if (input_buffer_rpos == input_buffer_size_max) input_buffer_rpos = 0;
		d->buf_len = len;
		pthread_rwlock_unlock(&d->rw);

		/* rotate and convert input - very fast */
		if (!d->offset_tuning)
		{
			rotate_90_u8_f32(d);
		}
		else
		{
			u8_f32(d);
		}

		/* wait for input data, demodulate - very slow */
		full_demod(d);

		if (d->exit_flag) {
      do_exit = 1;
		}

		/* squelch */
		if (d->squelch_level && d->squelch_hits > d->conseq_squelch)
		{
			d->squelch_hits = d->conseq_squelch + 1; /* hair trigger */
			safe_cond_signal(&controller.hop, &controller.hop_m);
			continue;
		}

		/* output */
		pthread_rwlock_wrlock(&o->rw);
		len = d->result_len << 1;
		if (output_buffer_wpos + len <= output_buffer_size_max)
		{
			memcpy(output_buffer + output_buffer_wpos, d->result, len);
			output_buffer_wpos += len;
			output_buffer_size += len;
			/* begin new read with zero */
			if (output_buffer_wpos >= output_buffer_size_max) output_buffer_wpos = 0;
		}
		else
		{
			/* buffer_size_max must be multiple of len */
			memcpy(output_buffer, d->result, len);
			output_buffer_wpos = len;
			output_buffer_size += len;
		}
		/* already dropped some data, so print info */
		if (output_buffer_size > output_buffer_size_max)
		{
			fprintf(stderr, "dropping output buffer: %u B\n", output_buffer_size - output_buffer_size_max);
			output_buffer_size = output_buffer_size_max;
		}
		pthread_rwlock_unlock(&o->rw);
		
	}


	return 0;
}

static void * output_thread_fn(void *arg)
{

  int circbufferbotton;
  int circbufferout;
  int shiftmin;
  int shiftmax;
  char circbufferfull;
  
	int SentNum = 0;
	struct output_state *s = arg;
	char buf[16384];
	uint32_t len = 16384;

  circbufferbotton=0;
  circbufferout=0;
  circbufferfull=0;
  shiftmin=0;
  shiftmax=0;

	while (!do_exit)
	{
		while (output_buffer_size < len)
		{
			if (do_exit) return 0;
			usleep(5000);
		}

    // copy output_buffer to local buffer, this must be fast because of thread locks
		pthread_rwlock_rdlock(&s->rw);
		memcpy(buf, output_buffer + output_buffer_rpos, len);
		output_buffer_rpos += len;
		output_buffer_size -= len;
		if (output_buffer_rpos >= output_buffer_size_max) output_buffer_rpos = 0;
		pthread_rwlock_unlock(&s->rw);

    // copy local buffer to circular timeshift buffer
    memcpy(circbuffer+(circbufferbotton*16384), buf, 16384);

		if (isStartStream)
		{

      if (circbufferfull==0)
        shiftmin=0;
      else {
        shiftmin = circbufferbotton+1;
        if (shiftmin >= circbufferslots) {
          shiftmin=0;
        }
      }
      shiftmax=circbufferbotton;

      if (circbuffeshift < 0) circbuffeshift=0;

      // max shift time available
      if (circbufferfull==0) {
        if (circbuffeshift > circbufferbotton)
          circbuffeshift=circbufferbotton;
      } else {
        if (circbuffeshift > (circbufferslots-1))
          circbuffeshift = (circbufferslots-1);
      }

      circbufferout = (circbufferbotton-circbuffeshift);

      if (circbufferout < 0) {
        circbufferout  = circbufferslots - (circbuffeshift-circbufferbotton);
      }


//      if (output.filename!=0) {
//        SentNum = fwrite (buf , sizeof(char), len, output.file);
//      } else {
        SentNum = zplay_PushDataToStream(libzplay, circbuffer+(circbufferout*16384), len);
//      }

      if (++circbufferbotton >= circbufferslots) {
        circbufferfull=1;
        circbufferbotton=0;
      }

      
			if (SentNum == 0)
			{
				fprintf(stderr, "Error sending stream: \"%s\". Close the connection!\n", zplay_GetError(libzplay) );
				isStartStream = false;
				// Stop reading samples from dongle
				rtlsdr_cancel_async(dongle.dev);
				pthread_join(dongle.thread, NULL);
			}
		} // isStartStream
		
	} // do_exit
	
	if (isStartStream) {
    zplay_Stop(libzplay);
    isStartStream=false;
	}

	return 0;
}



static void optimal_settings(int freq, int rate)
{
	// giant ball of hacks
	// seems unable to do a single pass, 2:1
	int capture_freq, capture_rate;
	struct dongle_state *d = &dongle;
	struct demod_state *dm = &demod;
	struct controller_state *cs = &controller;


	dm->downsample = 8;

	if (dm->downsample_passes)
	{
		dm->downsample_passes = (int) log2(dm->downsample) + 1;
		dm->downsample = 1 << dm->downsample_passes;
	}

	capture_freq = freq;
	capture_rate = dm->downsample * dm->rate_in;

	if (!dm->offset_tuning)
	{
		capture_freq = freq + capture_rate / 4;
	}

	capture_freq += cs->edge * dm->rate_in / 2;

	dm->output_scale = 1;

	d->freq = (uint32_t) capture_freq;
	d->rate = (uint32_t) capture_rate;
}

static void * controller_thread_fn(void *arg)
{
	// thoughts for multiple dongles
	// might be no good using a controller thread if retune/rate blocks
	int i;
	struct controller_state *s = arg;

	if (s->wb_mode) {
		for (i=0; i < s->freq_len; i++) {
			s->freqs[i] += 16000;}
	}

	/* set up primary channel */
	optimal_settings(s->freqs[0], demod.rate_in);
	if (dongle.direct_sampling) {
		verbose_direct_sampling(dongle.dev, 1);}

	/* Set the frequency */
	verbose_set_frequency(dongle.dev, dongle.freq);
	fprintf(stderr, "Oversampling input by: %ix.\n", demod.downsample);
	fprintf(stderr, "Oversampling output by: %ix.\n", demod.post_downsample);
	fprintf(stderr, "Buffer size: %0.2fms\n",
		1000 * 0.5 * (float)ACTUAL_BUF_LENGTH / (float)dongle.rate);

	/* Set the sample rate */
	verbose_set_sample_rate(dongle.dev, dongle.rate);
	fprintf(stderr, "Output at %u Hz.\n", demod.rate_in/demod.post_downsample);

	while (!do_exit) {
		safe_cond_wait(&s->hop, &s->hop_m);
		if (s->freq_len <= 1) {
			continue;}
		/* hacky hopping */
		s->freq_now = (s->freq_now + 1) % s->freq_len;
		optimal_settings(s->freqs[s->freq_now], demod.rate_in);
		rtlsdr_set_center_freq(dongle.dev, dongle.freq);
		dongle.mute = BUFFER_DUMP;
	}
	return 0;
}

void frequency_range(struct controller_state *s, char *arg)
{
	char *start, *stop, *step;
	int i;
	start = arg;
	stop = strchr(start, ':') + 1;
	if (stop == (char *)1) { // no stop or step given
		s->freqs[s->freq_len] = (uint32_t) atofs(start);
		s->freq_len++;
		return;
	}
	stop[-1] = '\0';
	step = strchr(stop, ':') + 1;
	if (step == (char *)1) { // no step given
		s->freqs[s->freq_len] = (uint32_t) atofs(start);
		s->freq_len++;
		s->freqs[s->freq_len] = (uint32_t) atofs(stop);
		s->freq_len++;
		stop[-1] = ':';
		return;
	}
	step[-1] = '\0';
	for(i=(int)atofs(start); i<=(int)atofs(stop); i+=(int)atofs(step))
	{
		s->freqs[s->freq_len] = (uint32_t)i;
		s->freq_len++;
		if (s->freq_len >= FREQUENCIES_LIMIT) {
			break;}
	}
	stop[-1] = ':';
	step[-1] = ':';
}

void dongle_init(struct dongle_state *s)
{
	s->rate = DEFAULT_SAMPLE_RATE;
	s->gain = AUTO_GAIN; // tenths of a dB
	s->mute = 0;
	s->direct_sampling = 0;
	s->demod_target = &demod;
}

void demod_init(struct demod_state *s)
{
	s->rate_in = DEFAULT_SAMPLE_RATE;
	s->rate_out = DEFAULT_SAMPLE_RATE;
	s->squelch_level = 0;
	s->conseq_squelch = 10;
	s->terminate_on_squelch = 0;
	s->squelch_hits = 11;
	s->downsample_passes = 0;
	s->comp_fir_size = 0;
	s->prev_index = 0;
	s->post_downsample = 1;  // once this works, default = 4
	s->custom_atan = 1;
	s->deemph = DEEMPHASIS_FM_EU;
	s->offset_tuning = 0;
	s->rate_out2 = 48000;
	s->pre_j = s->pre_r = s->now_r = s->now_j = 0;
	s->pre_j_f32 = s->pre_r_f32 = 0;
	s->prev_lpr_index = 0;
	s->deemph = DEEMPHASIS_FM_EU;
	s->deemph_a = 0;
	s->deemph_l = 0;
	s->deemph_r = 0;
	s->deemph_l_f32 = 0;
	s->deemph_r_f32 = 0;
	s->volume = 0.4f;
	s->now_lpr = 0;
	s->lpr.mode = 2;
	s->lpr.size = 90; /* RPI can do only 90, 128 is optimal */
	s->lpr.br = NULL;
	s->lpr.bm = NULL;
	s->lpr.bs = NULL;
	s->lpr.fm = NULL;
	s->lpr.fp = NULL;
	s->lpr.fs = NULL;
	pthread_rwlock_init(&s->rw, NULL);
	pthread_cond_init(&s->ready, NULL);
	pthread_mutex_init(&s->ready_m, NULL);
	s->output_target = &output;
}

void demod_cleanup(struct demod_state *s)
{
	pthread_rwlock_destroy(&s->rw);
	pthread_cond_destroy(&s->ready);
	pthread_mutex_destroy(&s->ready_m);
}

void output_init(struct output_state *s)
{
	s->rate = 48000;
	pthread_rwlock_init(&s->rw, NULL);
	pthread_cond_init(&s->ready, NULL);
	pthread_mutex_init(&s->ready_m, NULL);
}

void output_cleanup(struct output_state *s)
{
	pthread_rwlock_destroy(&s->rw);
	pthread_cond_destroy(&s->ready);
	pthread_mutex_destroy(&s->ready_m);
}

void controller_init(struct controller_state *s)
{
	s->freqs[0] = 100000000;
	s->freq_len = 0;
	s->edge = 0;
	s->wb_mode = 1; // Set to wbfm
	pthread_cond_init(&s->hop, NULL);
	pthread_mutex_init(&s->hop_m, NULL);
}

void controller_cleanup(struct controller_state *s)
{
	pthread_cond_destroy(&s->hop);
	pthread_mutex_destroy(&s->hop_m);
}


void sanity_checks(void)
{
	if (controller.freq_len == 0) {
//		fprintf(stderr, "Please specify a frequency.\nUse -h parameter for help.\n");
    controller.freqs[controller.freq_len++] = 88000000;
//		exit(1);
	}

	if (controller.freq_len >= FREQUENCIES_LIMIT) {
		fprintf(stderr, "Too many channels, maximum %i.\n", FREQUENCIES_LIMIT);
		exit(1);
	}

	if (controller.freq_len > 1 && demod.squelch_level == 0) {
		fprintf(stderr, "Please specify a squelch level.  Required for scanning multiple frequencies.\n");
		exit(1);
	}

}


int _getch(void)
{
// https://bitismyth.wordpress.com/2015/06/10/um-getch-multiplataforma/
#ifdef __linux
  struct termios newtios, oldtios;
  int ch;
 
  tcgetattr(STDIN_FILENO, &oldtios);
 
  /* Reusa o modo atual. */
  newtios = oldtios;
 
  /* Desabilita o modo canonico e o echo */
  newtios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newtios);
 
  ch = getchar();
 
  /* Retorna o terminal ao normal. */
  tcsetattr(STDIN_FILENO, TCSANOW, &oldtios);
 
  return ch;
#else
  // Usa as funções do console do Windows para ler uma tecla.
  //
  HANDLE hStdin = GetStdHandle (STD_INPUT_HANDLE);
  INPUT_RECORD irInputRecord;
  DWORD dwEventsRead;
  int ch;
 
  /* Le keypress... */
  while (ReadConsoleInputA(hStdin, &irInputRecord, 1, &dwEventsRead))
    if (irInputRecord.EventType == KEY_EVENT &&
    irInputRecord.Event.KeyEvent.wVirtualKeyCode != VK_SHIFT &&
    irInputRecord.Event.KeyEvent.wVirtualKeyCode != VK_MENU &&
    irInputRecord.Event.KeyEvent.wVirtualKeyCode != VK_CONTROL)
    {
      ch = irInputRecord.Event.KeyEvent.uChar.AsciiChar;
 
      /* ... e, finalmente, le o key release! */
      ReadConsoleInputA (hStdin, &irInputRecord , 1, &dwEventsRead);
      return ch;
    }

  return EOF;
#endif
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
  int keybrd;
	int opt;
  int librtlerr;
	int dev_given = 0;
	int custom_ppm = 0;
  int libzplayerr;
  int enable_biastee = 0;
  int circbuffersize;
  int reprintline;
  int outputFormat;
  int muted;
  int recording;
  struct tm *timeinfo;
  time_t rawtime;
  char infostr[16];
  char fileUniqueStr[35];
  char *filenameExt;


  // timeshift buffer size in kbytes
  circbuffersize = 10240;
  circbufferslots = (circbuffersize * 1024) / 16384;
  circbuffeshift=0;

	fprintf(stderr, "rtl_fm_player Version %s\n", VERSION);


  // initialize libzplay
  libzplay = zplay_CreateZPlay();
	// chek if we have class instance
	if(libzplay == 0) {
		fprintf(stderr,"Can't initialize libZPlay !\n");
    exit(1);
	}
	int libzplayver = zplay_GetVersion(libzplay);
  fprintf(stderr,"libZPlay v.%i.%02i\r\n\r\n", libzplayver / 100, libzplayver % 100);


	dongle_init(&dongle);
	demod_init(&demod);
	output_init(&output);
	controller_init(&controller);

	isStartStream = false;

	while((opt = getopt(argc, argv, "d:f:g:s:b:l:o:t:r:p:E:F:h:v:XYT")) != -1)
	{
		switch (opt)
		{
		case 'd':
			dongle.dev_index = verbose_device_search(optarg);
			dev_given = 1;
			break;
		case 'f':
			if (controller.freq_len >= FREQUENCIES_LIMIT) {
				break;}
			if (strchr(optarg, ':'))
				{frequency_range(&controller, optarg);}
			else
			{
				controller.freqs[controller.freq_len] = (uint32_t)atofs(optarg);
				controller.freq_len++;
			}
			break;
		case 'g':
			dongle.gain = (int) (atof(optarg) * 10);
			break;
		case 'l':
			demod.squelch_level = (int) atof(optarg);
			break;
		case 's':
			demod.rate_in = (uint32_t) atofs(optarg);
			demod.rate_out = (uint32_t) atofs(optarg);
			break;
		case 'r':
			output.rate = (int) atofs(optarg);
			demod.rate_out2 = (int) atofs(optarg);
			break;
		case 'o':
			fprintf(stderr, "Warning: -o is very buggy\n");
			demod.post_downsample = (int) atof(optarg);
			if (demod.post_downsample < 1 || demod.post_downsample > MAXIMUM_OVERSAMPLE)
			{
				fprintf(stderr, "Oversample must be between 1 and %i\n",
				MAXIMUM_OVERSAMPLE);
			}
			break;
		case 't':
			demod.conseq_squelch = (int) atof(optarg);
			if (demod.conseq_squelch < 0)
			{
				demod.conseq_squelch = -demod.conseq_squelch;
				demod.terminate_on_squelch = 1;
			}
			break;
		case 'p':
			dongle.ppm_error = atoi(optarg);
			custom_ppm = 1;
			break;
		case 'E':
			if (strcmp("edge", optarg) == 0)
			{
				controller.edge = 1;
			}
			if (strcmp("deemp", optarg) == 0)
			{
				demod.deemph = DEEMPHASIS_FM_EU;
			}
			if (strcmp("direct", optarg) == 0)
			{
				dongle.direct_sampling = 1;
			}
			if (strcmp("offset", optarg) == 0)
			{
				demod.offset_tuning = 1;
			}
			break;
		case 'F':
			demod.downsample_passes = 1;  // truthy placeholder
			demod.comp_fir_size = atoi(optarg);
			break;

		case 'X':
			fprintf(stderr, "Start with float FM stereo support\n");
			controller.wb_mode = 1;
			demod.rate_in = 192000;
			demod.rate_out = 192000;
			output.rate = 48000;
			demod.rate_out2 = 48000;
			demod.deemph = DEEMPHASIS_FM_EU;
			demod.squelch_level = 0;
			demod.lpr.mode = 2;
			/* RPI can do only 90, 128 is optimal */
			demod.lpr.size = 90;
			break;
		case 'Y':
			fprintf(stderr, "Start with float FM mono support\n");
			controller.wb_mode = 1;
			demod.rate_in = 192000;
			demod.rate_out = 192000;
			output.rate = 48000;
			demod.rate_out2 = 48000;
			demod.deemph = DEEMPHASIS_FM_EU;
			demod.squelch_level = 0;
			demod.lpr.mode = 1;
			demod.lpr.size = 128;
			break;

		case 'T':
			enable_biastee = 1;
			break;

		case 'v':
			printf(VERSION);
			break;

		case 'h':
		default:
			usage();
			break;
		}
	}
	/* quadruple sample_rate to limit to Δθ to ±π/2 */
	demod.rate_in *= demod.post_downsample;

	if (!output.rate)
	{
		output.rate = demod.rate_out;
	}

	sanity_checks();

	if (controller.freq_len > 1) {
		demod.terminate_on_squelch = 0;
	}

	if (argc <= optind) {
		output.filename = 0;
	} else {
		output.filename = argv[optind];
    filenameExt = strrchr(output.filename, '.');
	}

	ACTUAL_BUF_LENGTH = lcm_post[demod.post_downsample] * DEFAULT_BUF_LENGTH;

	if (!dev_given) {
		dongle.dev_index = verbose_device_search("0");
	}

	if (dongle.dev_index < 0) {
		exit(1);
	}

  // allocate timeshift buffer
	fprintf(stderr, "allocating %u bytes\n", circbufferslots * 16384);
  circbuffer = (char *)malloc(circbufferslots * 16384);
  if (circbuffer==0) {
		fprintf(stderr,"Can't allocate memmory for timeshift function\n");
    exit(1);  
  }


	librtlerr = rtlsdr_open(&dongle.dev, (uint32_t) dongle.dev_index);
	if (librtlerr < 0) {
    free(circbuffer);
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dongle.dev_index);
		exit(1);
	}
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif

	if (demod.deemph) {
		//fprintf(stderr, "De-epmhasis IIR: %.1f us\n", demod.deemph * 1e6);
		demod.deemph_a = (int) lrint(1.0 / ((1.0 - exp(-1.0 / ((double) demod.rate_out * demod.deemph)))));
		demod.deemph_lambda = (float) exp(-1.0 / ((double) output.rate * demod.deemph));
	}

	/* Set the tuner gain */
	if (dongle.gain == AUTO_GAIN) {
		verbose_auto_gain(dongle.dev);
	} else {
		dongle.gain = nearest_gain(dongle.dev, dongle.gain);
		verbose_gain_set(dongle.dev, dongle.gain);
	}

	rtlsdr_set_bias_tee(dongle.dev, enable_biastee);
	if (enable_biastee)
		fprintf(stderr, "activated bias-T on GPIO PIN 0\n");

	verbose_ppm_set(dongle.dev, dongle.ppm_error);

/*
  // write WAV output to file
  if (output.filename ==0) {
    fprintf(stderr, "No output file\n");
  } else {
    fprintf(stderr, "Sending WAV output to %s\n", output.filename);

    if (strcmp(output.filename, "-") == 0)
    { 
      fprintf(stderr, "Opening STDOUT\n");
      output.file = stdout;
#ifdef _WIN32
      _setmode(_fileno(output.file), _O_BINARY);
#endif
    }
    else if (output.filename!=0)
    {
      output.file = fopen(output.filename, "wb");
      if (!output.file) {
        free(circbuffer);
        fprintf(stderr, "Failed to open %s\n", output.filename);
        exit(1);
      }
    }
  } // filename
*/


	// Init FM float demodulator
	init_u8_f32_table();
	init_lp_f32();
	init_lp_real_f32(&demod);


	/* Reset endpoint before we start reading from it (mandatory) */
	verbose_reset_buffer(dongle.dev);

	pthread_create(&controller.thread, NULL, controller_thread_fn, (void *) (&controller));
	usleep(100000);
	pthread_create(&output.thread, NULL, output_thread_fn, (void *) (&output));
	pthread_create(&demod.thread, NULL, demod_thread_fn, (void *) (&demod));


  // Start reading samples from dongle
	pthread_create(&dongle.thread, NULL, dongle_thread_fn, (void *) (&dongle));

	optimal_settings(controller.freqs[controller.freq_len-1], demod.rate_in);
	verbose_set_frequency(dongle.dev, dongle.freq);


  zplay_SetSettings(libzplay, sidAccurateLength, 0);
  zplay_SetSettings(libzplay, sidWaveBufferSize , 1500);

  if (demod.lpr.mode==2) {
    fprintf(stderr, "Starting Stereo output\n");
    libzplayerr = zplay_OpenStream(libzplay, 1, 1, WAVHeaderStereo, sizeof(WAVHeaderStereo), sfWav);
  }  else {
    fprintf(stderr, "Starting Mono output\n");
    libzplayerr = zplay_OpenStream(libzplay, 1, 1, WAVHeaderMono, sizeof(WAVHeaderMono), sfWav);
  }

  if (libzplayerr==0) {
    fprintf(stderr, "Error opening stream: \"%s\". \n", zplay_GetError(libzplay) );
    do_exit=1;
  } else {

    if (output.filename!=0) {
      outputFormat=sfMp3;
         
      if (filenameExt && (!strcmp(filenameExt, ".mp3")))
        outputFormat = sfMp3;
      else if (filenameExt && (!strcmp(filenameExt, ".ogg")))
        outputFormat = sfOgg;
      else if (filenameExt && (!strcmp(filenameExt, ".wav")))
        outputFormat = sfWav;
      else if (filenameExt && (!strcmp(filenameExt, ".flac")))
        outputFormat = sfFLAC;
      else if (filenameExt && (!strcmp(filenameExt, ".aac")))
        outputFormat = sfAacADTS;

    
      libzplayerr = zplay_SetWaveOutFile(libzplay, output.filename, outputFormat , 0);
      if (libzplayerr==0) {
        fprintf(stderr, "Error saving to file. \"%s\". \n", zplay_GetError(libzplay) );
        output.filename=0;
      }
    }

    libzplayerr = zplay_Play(libzplay);
    if (libzplayerr!=0) {
      isStartStream = true;
    } else {
      do_exit=1;
    }
  
  } // noerror

  //////////////////// MAIN LOOP //////////////////////

  reprintline=1;
  muted=0;
  recording=0;


  usleep(500000);


  if (output.filename==0) {
    printf("\n[KEYS] \nA: +100KHz Z: -100KHz \nQ: TimeShift[Past] W: TimeShift[Present] E: TimeShift[Live] \nM: Mute/Unmute \nR: Record \nX: Exit\n\n");
  } else {
    printf("\n[KEYS] \nX: Exit\n\n");
    printf("Set frequency to %.2f MHz\n", ((float)((int)(controller.freqs[controller.freq_len-1] / 10000)) / 100.0));
    printf("Controls disabled. Saving audio to %s\n\n",output.filename);
    reprintline=0;
  }


	while (!do_exit)
	{
   	usleep(80000);

    if (circbuffeshift <= 0) {
      strcpy(infostr,"[Live] ");
    } else {
      strcpy(infostr,"[TimeShift] ");
    }
    if (muted) {
      strcat(infostr, "[muted] ");
      if (recording)
        strcat(infostr, "[rec] ");
      else
        strcat(infostr, "      ");
    } else {
      if (recording)
        strcat(infostr, "[rec]       ");
      else
        strcat(infostr, "            ");    
//      strcat(infostr, "        ");    
    }



    if (reprintline) {
      printf("\rSet frequency to %.2f MHz %s", ((float)((int)(controller.freqs[controller.freq_len-1] / 10000)) / 100.0) , infostr );
      reprintline=0;
    }

    keybrd=_getch();
    fprintf(stderr,"key=%d",keybrd);       

    if (output.filename==0) {

    if ((keybrd==97) || (keybrd==65)) { // A
      controller.freqs[controller.freq_len-1] += 100000;
      optimal_settings(controller.freqs[controller.freq_len-1], demod.rate_in);
      rtlsdr_set_center_freq(dongle.dev, dongle.freq);
      circbuffeshift=0;
      reprintline=1;
    }
    if ((keybrd==122) || (keybrd==90)) { // Z
      controller.freqs[controller.freq_len-1] -= 100000;
      optimal_settings(controller.freqs[controller.freq_len-1], demod.rate_in);
      rtlsdr_set_center_freq(dongle.dev, dongle.freq);
      circbuffeshift=0;
      reprintline=1;
    }
    if ((keybrd==113) || (keybrd==81)) { // Q
      circbuffeshift+=10;
      reprintline=1;
    }
    if ((keybrd==119) || (keybrd==87)) { // W
      circbuffeshift-=10;
      reprintline=1;
    }
    if ((keybrd==101) || (keybrd==69)) { // E
      circbuffeshift=0;
      reprintline=1;
    }
    if ((keybrd==109) || (keybrd==77)) { // M
      if (!muted) {
        zplay_SetPlayerVolume(libzplay, 0, 0);
        muted=1;
      } else {
        zplay_SetPlayerVolume(libzplay, 100, 100);
        muted=0;
      }
      reprintline=1;
    }
    if ((keybrd==114) || (keybrd==82)) { // R
      if (!recording) {
        time ( &rawtime );
        timeinfo = localtime ( &rawtime );
        strftime(fileUniqueStr, 34,"Recording_%Y-%m-%d_%H-%M.mp3",timeinfo);
        libzplayerr = zplay_SetWaveOutFile(libzplay, fileUniqueStr, sfMp3 , 1);
        if (libzplayerr==0) {
          fprintf(stderr, "Error saving to file. \"%s\". \n", zplay_GetError(libzplay) );
          output.filename=0;
        } else {
          libzplayerr = zplay_Play(libzplay);
          if (libzplayerr==0) {
            fprintf(stderr, "Error saving to file. \"%s\". \n", zplay_GetError(libzplay) );
          } else {
            recording=1;
          }
        }
        reprintline=1;
      } // recording
    }

    } // output.filename

    if ((keybrd==120) || (keybrd==88)) { // X
      do_exit = 1;
      fflush(stdin); 
    }


	} // while !do_exit
  /////////////////////////////////////////////////////



	if (do_exit) {
		fprintf(stderr, "\nUser cancel, exiting...\n");
	} else {
		fprintf(stderr, "\nLibrary error, exiting...\n" );
	}

  fprintf(stderr, "Closing threads\n");

  // first, wait for dongle thread
	pthread_join(dongle.thread, NULL); 
	safe_cond_signal(&demod.ready, &demod.ready_m);
	pthread_join(demod.thread, NULL);
	safe_cond_signal(&output.ready, &output.ready_m);
	pthread_join(output.thread, NULL);
	safe_cond_signal(&controller.hop, &controller.hop_m);
	pthread_join(controller.thread, NULL);

//  if (output.filename==0)
    zplay_Close(libzplay);
	
	fprintf(stderr, "Closing demodulator\n");	
	demod_cleanup(&demod);
  fprintf(stderr, "Closing output\n");	
	output_cleanup(&output);
  fprintf(stderr, "Closing controller\n");		
	controller_cleanup(&controller);

/*
  if (output.filename!=0)
	{
	  fprintf(stderr, "Closing output file\n");		
    // Fixing WAV file header
    // http://www.topherlee.com/software/pcm-tut-wavformat.html
    if (output.file != stdout) {
      long int outfilesize = ftell (output.file);
      fseek ( output.file, 4, SEEK_SET );
      outfilesize-=8;
      fputc(outfilesize & 0xff,output.file);
      fputc((outfilesize >> 8) & 0xff,output.file);
      fputc((outfilesize >> 16) & 0xff,output.file);
      fputc((outfilesize >> 24) & 0xff,output.file);
      fseek ( output.file, 40, SEEK_SET );
      outfilesize-=36;
      fputc(outfilesize & 0xff,output.file);
      fputc((outfilesize >> 8) & 0xff,output.file);
      fputc((outfilesize >> 16) & 0xff,output.file);
      fputc((outfilesize >> 24) & 0xff,output.file);
    } // if
		fclose(output.file);	
	}
*/

  free(circbuffer);

  fprintf(stderr, "Closing dongle\n");		
	rtlsdr_close(dongle.dev);

	return librtlerr >= 0 ? librtlerr : -librtlerr;
}

