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
#include <sys/types.h>
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

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "rtl_fm_player.h"

#include "convenience/convenience.h"
#include "rtl-sdr.h"

#define VERSION "0.0.1"



void usage(void)
{
  fprintf(
  stderr, "rtl_fm_player, a simple narrow band FM demodulator for RTL2832 based DVB-T receivers\n\n"
      "Use:\trtl_fm_player -f freq [-options] [filename]\n"
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
      "\tfilename (.wav file format)\n"
      "\t[-X Start with FM Stereo support]\n"
      "\t[-Y Start with FM Mono support]\n"
      "\t[-V verbose]\n"
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
    if (_beverbose)
      fprintf(stderr, "\nSignal caught, exiting!\n");
    _do_exit = 1;
    fflush(stdin);
    return TRUE;
  }
  return FALSE;
}
#else
static void sighandler(int signum)
{
  if (_beverbose)
    fprintf(stderr, "\nSignal caught, exiting!\n");
  _do_exit = 1;
  fflush(stdin); 
}
#endif

/* more cond dumbness */
#define safe_cond_signal(n, m) pthread_mutex_lock(m); pthread_cond_signal(n); pthread_mutex_unlock(m)
#define safe_cond_wait(n, m) pthread_mutex_lock(m); pthread_cond_wait(n, m); pthread_mutex_unlock(m)


#ifdef _MSC_VER
double log2(double n)
{
  return log(n) / log(2.0);
}
#endif


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
   while(PeekConsoleInputA(hStdin,&irInputRecord,1,&dwEventsRead)){
        if(dwEventsRead!=0){
            //check if it was Key Event
            if(irInputRecord.EventType==KEY_EVENT){
                ch=irInputRecord.Event.KeyEvent.uChar.AsciiChar; 
                ReadConsoleInputA(hStdin,&irInputRecord,1,&dwEventsRead);
                FlushConsoleInputBuffer(hStdin);
              if (irInputRecord.Event.KeyEvent.bKeyDown!=0)   // not key release
                return ch;
              else
                continue;
            }
            if(ReadConsoleInputA(hStdin,&irInputRecord,1,&dwEventsRead)){
                continue; 
            }
        } else
        WaitForSingleObject(hStdin,1000);
    }
  return EOF;
#endif
}


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

  if (_beverbose)
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

  if (_do_exit) { 
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
  if (_input_buffer_wpos + len <= _input_buffer_size_max)
  {
    memcpy(_input_buffer + _input_buffer_wpos, buf, len);
    _input_buffer_wpos += len;
    _input_buffer_size += len;
    /* begin new read with zero */
    if (_input_buffer_wpos == _input_buffer_size_max) _input_buffer_wpos = 0;
  }
  else
  {
    /* buffer_size_max must be multiple of len */
    memcpy(_input_buffer, buf, len);
    _input_buffer_wpos = len;
    _input_buffer_size += len;
  }
  /* already droped some data, so print info */
  if (_input_buffer_size > _input_buffer_size_max)
  {
    if (_beverbose)
      fprintf(stderr, "dropping input buffer: %u B\n", _input_buffer_size - _input_buffer_size_max);
    _input_buffer_size = _input_buffer_size_max;
  }
  pthread_rwlock_unlock(&d->rw);
  //safe_cond_signal(&d->ready, &d->ready_m);
}

static void * dongle_thread_fn(void *arg)
{
  int r = 0;
  struct dongle_state *s = arg;

  if (_do_exit) return 0;

  r= rtlsdr_read_async(s->dev, rtlsdr_callback, s, 0, 0);
  if (r < 0) {
      fprintf(stderr, "\nError reading from device.\nPress any key to exit.\n");
      _do_exit=1;
  }
  
  return 0;
}

static void * demod_thread_fn(void *arg)
{
  struct demod_state *d = arg;
  struct output_state *o = d->output_target;
  uint32_t len;

  while (!_do_exit)
  {
    len = MAXIMUM_BUF_LENGTH;
    while (_input_buffer_size < len)
    {
      if ((d->exit_flag) || (_do_exit)) return 0;
      usleep(5000);
    }

    pthread_rwlock_wrlock(&d->rw);
    memcpy(d->buf, _input_buffer + _input_buffer_rpos, len);
    _input_buffer_rpos += len;
    _input_buffer_size -= len;
    if (_input_buffer_rpos == _input_buffer_size_max) _input_buffer_rpos = 0;
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
      _do_exit = 1;
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
    if (_output_buffer_wpos + len <= _output_buffer_size_max)
    {
      memcpy(_output_buffer + _output_buffer_wpos, d->result, len);
      _output_buffer_wpos += len;
      _output_buffer_size += len;
      /* begin new read with zero */
      if (_output_buffer_wpos >= _output_buffer_size_max) _output_buffer_wpos = 0;
    }
    else
    {
      /* buffer_size_max must be multiple of len */
      memcpy(_output_buffer, d->result, len);
      _output_buffer_wpos = len;
      _output_buffer_size += len;
    }
    /* already dropped some data, so print info */
    if (_output_buffer_size > _output_buffer_size_max)
    {
      if (_beverbose)
        fprintf(stderr, "dropping output buffer: %u B\n", _output_buffer_size - _output_buffer_size_max);
      _output_buffer_size = _output_buffer_size_max;
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
  int SentNum;
  int circbufferfull;
  struct output_state *s = arg;


  circbufferbotton=0;
  circbufferout=0;
  circbufferfull=0;
  shiftmin=0;
  shiftmax=0;
  SentNum=0;

  while (!_do_exit)
  {
    while (_output_buffer_size < CIRCBUFFCLUSTER)
    {
      if (_do_exit) return 0;
      usleep(5000);
    }


    // copy block to circular buffer
    pthread_rwlock_rdlock(&s->rw);
    memcpy(_circbuffer+(circbufferbotton*CIRCBUFFCLUSTER), _output_buffer + _output_buffer_rpos, CIRCBUFFCLUSTER);
    _output_buffer_rpos += CIRCBUFFCLUSTER;
    _output_buffer_size -= CIRCBUFFCLUSTER;
    if (_output_buffer_rpos >= _output_buffer_size_max) _output_buffer_rpos = 0;
    pthread_rwlock_unlock(&s->rw);

    if (_isStartStream)
    {
      if (circbufferfull==0)
        shiftmin=0;
      else {
        shiftmin = circbufferbotton+1;
        if (shiftmin >= _circbufferslots) {
          shiftmin=0;
        }
      }
      shiftmax=circbufferbotton;

      if (_circbuffeshift < 0) _circbuffeshift=0;

      // max shift time available
      if (circbufferfull==0) {
        if (_circbuffeshift > circbufferbotton)
          _circbuffeshift=circbufferbotton;
      } else {
        if (_circbuffeshift > (_circbufferslots-2))
          _circbuffeshift = (_circbufferslots-2);
      }

      // calculate circular buffer playback position
      circbufferout = (circbufferbotton-_circbuffeshift);
      if (circbufferout < 0) {
        circbufferout  = _circbufferslots - (_circbuffeshift-circbufferbotton);
      }

      if (!_audio_muted) {
        SentNum = SDL_QueueAudio(_audio_device, _circbuffer+(circbufferout*CIRCBUFFCLUSTER), CIRCBUFFCLUSTER);
      }

      if (output.filename!=0) {
        fwrite (_circbuffer+(circbufferout*CIRCBUFFCLUSTER) , sizeof(char), CIRCBUFFCLUSTER, output.file);
      }

      if (++circbufferbotton >= _circbufferslots) {
        circbufferfull=1;
        circbufferbotton=0;
      }

      
      if (SentNum != 0) {
        fprintf(stderr, "Error sending stream: \"%s\". Close the connection!\n", SDL_GetError() );
        _isStartStream = false;
        // Stop reading samples from dongle
        rtlsdr_cancel_async(dongle.dev);
        pthread_join(dongle.thread, NULL);
        fprintf(stderr,"Press [X] to exit");
      }
    } // _isStartStream

  } // _do_exit

  if (_isStartStream) {
    _isStartStream=false;
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
    verbose_direct_sampling(dongle.dev, 1);
  }

  /* Set the frequency */
  if (_beverbose) {
    verbose_set_frequency(dongle.dev, dongle.freq);
    fprintf(stderr, "Oversampling input by: %ix.\n", demod.downsample);
    fprintf(stderr, "Oversampling output by: %ix.\n", demod.post_downsample);
    fprintf(stderr, "Buffer size: %0.2fms\n", 1000 * 0.5 * (float)ACTUAL_BUF_LENGTH / (float)dongle.rate);
  } else {
    if ( rtlsdr_set_center_freq(dongle.dev, dongle.freq) < 0)
      fprintf(stderr, "WARNING: Failed to set center freq. %u\n",dongle.freq);
  }
   
  /* Set the sample rate */
  if (_beverbose) {
    verbose_set_sample_rate(dongle.dev, dongle.rate);
    fprintf(stderr, "Output at %u Hz.\n", demod.rate_in/demod.post_downsample);
  } else {
    if ( rtlsdr_set_sample_rate(dongle.dev, dongle.rate) < 0 )
      fprintf(stderr, "WARNING: Failed to set sample rate.\n");
  }

  while (!_do_exit) {
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
    controller.freqs[controller.freq_len++] = 88000000;
  }

  if (controller.freq_len >= FREQUENCIES_LIMIT) {
    fprintf(stderr, "Too many channels, maximum %i.\n", FREQUENCIES_LIMIT);
    fprintf(stderr,"Press any key to exit\n");
    _getch();
    exit(1);
  }

  if (controller.freq_len > 1 && demod.squelch_level == 0) {
    fprintf(stderr, "Please specify a squelch level.  Required for scanning multiple frequencies.\n");
    fprintf(stderr,"Press any key to exit\n");
    _getch();
    exit(1);
  }

}

void CloseWaveOut(FILE * file)
{

  if (file!=NULL) {
    // Fixing WAV file header
    // http://www.topherlee.com/software/pcm-tut-wavformat.html
    if (file != stdout) {
      long int outfilesize = ftell(file);
      fseek ( file, 4, SEEK_SET );
      outfilesize-=8;
      fputc(outfilesize & 0xff,file);
      fputc((outfilesize >> 8) & 0xff,file);
      fputc((outfilesize >> 16) & 0xff,file);
      fputc((outfilesize >> 24) & 0xff,file);
      fseek ( file, 40, SEEK_SET );
      outfilesize-=36;
      fputc(outfilesize & 0xff,file);
      fputc((outfilesize >> 8) & 0xff,file);
      fputc((outfilesize >> 16) & 0xff,file);
      fputc((outfilesize >> 24) & 0xff,file);
    } // if
		fclose(file);
		file=NULL;
	} // if

}

FILE * InitWaveOut(char * newfile, int mode)
{
  FILE *file;
  size_t written;

  // write WAV output to file
  if (newfile ==0) {
    return NULL;
  } else {

    if (strcmp(newfile, "-") == 0)
    { 
      fprintf(stderr, "Opening STDOUT\n");
      file = stdout;
#ifdef _WIN32
      _setmode(_fileno(file), _O_BINARY);
#endif
    }
    else if (newfile!=0)
    {
      file = fopen(newfile, "wb");
      if (!file) {
        return NULL;
      }
    }
    if (mode==2) {  
      written = fwrite(_WAVHeaderStereo , sizeof(char), sizeof(_WAVHeaderStereo), file); // write STEREO WAV header
      if (written != sizeof(_WAVHeaderStereo)) {
        fclose(file);
        return NULL;
      }
    } else {
      written = fwrite(_WAVHeaderMono , sizeof(char), sizeof(_WAVHeaderMono), file); // write MONO WAV header
      if (written != sizeof(_WAVHeaderMono)) {
        fclose(file);
        return NULL;
      }
    }
    
    return file;
  } // newfile

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
  int enable_biastee = 0;
  int circbuffersize;
  int reprintline;
  int outputFormat;
  int recording;
  int charposition;
  int controldisabled;
  float newfrequency;
  struct tm *timeinfo;
  time_t rawtime;
  char infostr[22];
  char fileUniqueStr[35];
  char *filenameExt;

  SDL_AudioSpec audioFormatDesired;
  audioFormatDesired.freq = 48000;
  audioFormatDesired.format = AUDIO_S16LSB;
  audioFormatDesired.channels = 2;
  audioFormatDesired.samples = 4096;
  audioFormatDesired.callback = 0;
  SDL_AudioSpec audioFormatObtained;

  // timeshift buffer size in kbytes
  circbuffersize = 184320;
  _circbufferslots = (circbuffersize * 1024) / CIRCBUFFCLUSTER;
  _circbuffeshift=0;

#ifdef _WIN32
  SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#else
  nice(-5);
#endif

  printf("RTL FM Player Version %s (c) RafaelBF 2020.\n", VERSION);

  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
    fprintf(stderr,"Press any key to exit\n");
    _getch();
    exit(1);
  }

  dongle_init(&dongle);
  demod_init(&demod);
  output_init(&output);
  controller_init(&controller);

  _isStartStream = false;

  while((opt = getopt(argc, argv, "d:f:g:s:b:l:o:t:r:p:E:F:h:v:XYTV")) != -1)
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

    case 'V':
      fprintf(stderr, "Verbose mode\n");
      _beverbose=1;
      break;

    case 'h':
    default:
      usage();
      break;
    }
  }
  /* quadruple sample_rate to limit to Δθ to ±π/2 */
  demod.rate_in *= demod.post_downsample;

  if (!output.rate) {
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
    if (!filenameExt)
      strcat(output.filename, ".wav");
  }

  ACTUAL_BUF_LENGTH = lcm_post[demod.post_downsample] * DEFAULT_BUF_LENGTH;

  if (!dev_given) {
    dongle.dev_index = verbose_device_search("0");
  }

  if (dongle.dev_index < 0) {
    fprintf(stderr,"Press any key to exit\n");
    _getch();
    exit(1);
  }

  // allocate timeshift buffer
  if (_beverbose)
    fprintf(stderr, "Allocating %u bytes\n", _circbufferslots * CIRCBUFFCLUSTER);
  _circbuffer = (char *)malloc(_circbufferslots * CIRCBUFFCLUSTER);
  if (_circbuffer==0) {
    fprintf(stderr,"Can't allocate memmory for timeshift function\n");
    fprintf(stderr,"Press any key to exit\n");
    _getch();
    exit(1);  
  }


  librtlerr = rtlsdr_open(&dongle.dev, (uint32_t) dongle.dev_index);
  if (librtlerr < 0) {
    free(_circbuffer);
    fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dongle.dev_index);
    fprintf(stderr,"Press any key to exit\n");
    _getch();
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
    demod.deemph_a = (int) lrint(1.0 / ((1.0 - exp(-1.0 / ((double) demod.rate_out * demod.deemph)))));
    demod.deemph_lambda = (float) exp(-1.0 / ((double) output.rate * demod.deemph));
  }

  /* Set the tuner gain */
  if (dongle.gain == AUTO_GAIN) {
    if (_beverbose) {
      verbose_auto_gain(dongle.dev);
    } else {
      if ( rtlsdr_set_tuner_gain_mode(dongle.dev, 0) != 0 )
        fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
    }
  } else {
    dongle.gain = nearest_gain(dongle.dev, dongle.gain);
    verbose_gain_set(dongle.dev, dongle.gain);
  }

  rtlsdr_set_bias_tee(dongle.dev, enable_biastee);
  if (enable_biastee)
    if (_beverbose)
      fprintf(stderr, "activated bias-T on GPIO PIN 0\n");

  verbose_ppm_set(dongle.dev, dongle.ppm_error);

  // Init FM float demodulator
  init_u8_f32_table();
  init_lp_f32();
  init_lp_real_f32(&demod);


  /* Reset endpoint before we start reading from it (mandatory) */
  verbose_reset_buffer(dongle.dev);

  // start threads
  pthread_create(&controller.thread, NULL, controller_thread_fn, (void *) (&controller));
  usleep(100000);

  pthread_create(&output.thread, NULL, output_thread_fn, (void *) (&output));

  pthread_create(&demod.thread, NULL, demod_thread_fn, (void *) (&demod));

  // Start reading samples from dongle 
  pthread_create(&dongle.thread, NULL, dongle_thread_fn, (void *) (&dongle));

  optimal_settings(controller.freqs[controller.freq_len-1], demod.rate_in);
  if (_beverbose) {
    verbose_set_frequency(dongle.dev, dongle.freq);
  } else {
    rtlsdr_set_center_freq(dongle.dev, dongle.freq);
  }


  if (demod.lpr.mode==2) {
    if (_beverbose)
      fprintf(stderr, "Starting Stereo output\n"); 
  } else {
    if (_beverbose)
      fprintf(stderr, "Starting Mono output\n");
    audioFormatDesired.channels = 1;
  }

  _audio_device = SDL_OpenAudioDevice(NULL, 0, &audioFormatDesired, &audioFormatObtained, 0);
  if (_audio_device==0) {
    fprintf(stderr,"Could not retrieve a valid audio device: %s.\n", SDL_GetError());
    fprintf(stderr,"Press any key to exit\n");
    _getch();
    _do_exit=1;  
  } else {
    if (_beverbose)    
      fprintf(stderr,"Opened audio, device %s, freq %d, size %d, format %d, channels %d, samples %d", SDL_GetCurrentAudioDriver(),
             audioFormatObtained.freq, audioFormatObtained.size, audioFormatObtained.format, audioFormatObtained.channels, audioFormatObtained.samples);
  }


  // filename given at command line
  controldisabled=0;
  if (output.filename!=0) {
    output.file = InitWaveOut(output.filename,demod.lpr.mode);
    if (output.file==NULL) {
      output.filename=0;
      fprintf(stderr, "Error saving to file. %s\r", strerror( errno) );
    } else {
      controldisabled=1;
    }
  }

  SDL_PauseAudioDevice(_audio_device, 0);
  _isStartStream = true;

  //////////////////// MAIN LOOP //////////////////////

  reprintline=1;
  _audio_muted=0;
  recording=0;

  printf("\n+----------------------------------------------------------------------------+\n");
  printf("|                               RTL FM Player                                |\n");
  printf("+--------------------------------  k e y s ----------------------------------+\n");

  if (!controldisabled) {
    printf("| [W]: +50KHz [S]: -50KHz  [T]: Type a frequency                             |\n");
    printf("| [A]: TimeShift [Past]  [D]: TimeShift [Present]  [L]: TimeShift [Live]     |\n");
    printf("| [M]: Mute/Unmute                                                           |\n");
    printf("| [R]: Record/Stop                                                           |\n");
    printf("| [X]: Exit                                                                  |\n");
    printf("+----------------------------------------------------------------------------+\n\n");

  } else {
    printf("| [X]: Exit                                                                  |\n");
    printf("+----------------------------------------------------------------------------+\n\n");
    
    
    printf("  >>> %.2f MHz <<<\n", ((float)((int)(controller.freqs[controller.freq_len-1] / 10000)) / 100.0));
    printf("Controls disabled. Saving audio to %s\n\n",output.filename);
    reprintline=0;
  }


  while (!_do_exit)
  {

    // [TimeShift100%] [Mute] [Rec]
    if (_circbuffeshift <= 0) {
      strcpy(infostr,"[Live] ");
    } else {
      sprintf(infostr,"[TimeShift%u%%] ",((_circbuffeshift *100) / _circbufferslots));
    }
    if (_audio_muted) {
      strcat(infostr, "[Mute]  ");
      if (recording)
        strcat(infostr, "[Rec] ");
      else
        strcat(infostr, "      ");
    } else {
      if (recording)
        strcat(infostr, "[Rec]                ");
      else
        strcat(infostr, "                     ");    
    }          // [Live]
               // [TimeShift100%] [Mute] [Rec]


    if (reprintline) {
      printf("  >>> %.2f MHz <<<  %s\r", ((float)((int)(controller.freqs[controller.freq_len-1] / 10000)) / 100.0) , infostr );
      fflush(stdout);
      fflush(stdin);
      reprintline=0;
    }

    keybrd=_getch();
    if (_beverbose)
      fprintf(stderr,"key=%d",keybrd);       

    if (!controldisabled) {

      if ((keybrd==119) || (keybrd==87)) { // W
        controller.freqs[controller.freq_len-1] += 50000;
        if (controller.freqs[controller.freq_len-1] > 108000000)
          controller.freqs[controller.freq_len-1] = 76000000;
        optimal_settings(controller.freqs[controller.freq_len-1], demod.rate_in);
        if ( rtlsdr_set_center_freq(dongle.dev, dongle.freq) < 0 ) {
          fprintf(stderr, "WARNING: Failed to set center freq.\r");
        } else {
          _circbuffeshift=0;
          reprintline=1;
          if (SDL_GetQueuedAudioSize(_audio_device) > CIRCBUFFCLUSTER * 5)
            SDL_ClearQueuedAudio(_audio_device);
        }
      }
      if ((keybrd==115) || (keybrd==83)) { // S
        controller.freqs[controller.freq_len-1] -= 50000;
        if (controller.freqs[controller.freq_len-1] < 76000000)
          controller.freqs[controller.freq_len-1] = 108000000;
        optimal_settings(controller.freqs[controller.freq_len-1], demod.rate_in);
        if ( rtlsdr_set_center_freq(dongle.dev, dongle.freq) < 0 ) {
          fprintf(stderr, "WARNING: Failed to set center freq.\r");
        } else {
          _circbuffeshift=0;
          reprintline=1;
          if (SDL_GetQueuedAudioSize(_audio_device) > CIRCBUFFCLUSTER * 5)
            SDL_ClearQueuedAudio(_audio_device);
        }
      }
      if ((keybrd==116) || (keybrd==84)) { // T
        printf("                                                  \r"); // clear this line
        printf("Type the new frequency: ");
        newfrequency=0;      

        charposition=0;
        do {
          keybrd=_getch();
          if ( ((keybrd >= '0') && (keybrd <= '9')) || (keybrd == 46) || (keybrd == 44)  ) {
            infostr[charposition++] = keybrd;
            printf("%c", keybrd);
          } else if ( (keybrd==27) || (keybrd==13) || (keybrd==10) ) {
            keybrd=255;
          }        
        } while ( (keybrd!=255) && (charposition<5) );
        infostr[charposition]=0;

        printf("\r");
        newfrequency = atof(infostr);
        newfrequency*=1000000;

        if ( (newfrequency <= 108000000) && (newfrequency >= 76000000) ) {
          controller.freqs[controller.freq_len-1] = (uint32_t)newfrequency;
          optimal_settings(controller.freqs[controller.freq_len-1], demod.rate_in);

          if ( rtlsdr_set_center_freq(dongle.dev, dongle.freq) < 0 ) {
            fprintf(stderr, "WARNING: Failed to set center freq.\r");
          } else {
            _circbuffeshift=0;
            reprintline=1;
            if (SDL_GetQueuedAudioSize(_audio_device) > CIRCBUFFCLUSTER * 5)
              SDL_ClearQueuedAudio(_audio_device);
          }
        
          } else {
            reprintline=1;
        }
      } // if keybrd

      if ((keybrd==97) || (keybrd==65)) { // A
        _circbuffeshift+=20;
        reprintline=1;
        if (SDL_GetQueuedAudioSize(_audio_device) > CIRCBUFFCLUSTER * 5)
          SDL_ClearQueuedAudio(_audio_device);
      }
      if ((keybrd==100) || (keybrd==68)) { // D
        _circbuffeshift-=20;
        reprintline=1;
        if (SDL_GetQueuedAudioSize(_audio_device) > CIRCBUFFCLUSTER * 5)
          SDL_ClearQueuedAudio(_audio_device);
      }
      if ((keybrd==108) || (keybrd==76)) { // P
        _circbuffeshift=0;
        reprintline=1;
      }
      if ((keybrd==109) || (keybrd==77)) { // M
        if (!_audio_muted) {
          SDL_PauseAudioDevice(_audio_device, 1);
          _audio_muted=1;
        } else {
          if (SDL_GetQueuedAudioSize(_audio_device) > CIRCBUFFCLUSTER * 5)
            SDL_ClearQueuedAudio(_audio_device);
          SDL_PauseAudioDevice(_audio_device, 0);
          _audio_muted=0;
        }
        reprintline=1;
      }

      if ((keybrd==114) || (keybrd==82)) { // R
        if (!recording) {
          time ( &rawtime );
          timeinfo = localtime ( &rawtime );
          
          strftime(fileUniqueStr, 34,"FMrecord_%Y-%m-%d_%H-%M-%S.wav",timeinfo);
          output.file = InitWaveOut(fileUniqueStr,demod.lpr.mode);
          
          if (output.file==NULL) {
            fprintf(stderr, "Error saving to file. %s\r", strerror( errno) );
          } else {
            recording=1;
            reprintline=1;
            output.filename=fileUniqueStr;
          }
        } else { // recording
          output.filename=0;
          CloseWaveOut(output.file);
          recording=0;
          reprintline=1;
        }
      }
      
    } // controldisabled

    if ((keybrd==120) || (keybrd==88)) { // X
      _do_exit = 1;
      fflush(stdin); 
    }

  } // while !_do_exit
  /////////////////////////////////////////////////////



  if (_do_exit) {
    fprintf(stderr, "\nUser cancel, exiting...\n");
    } else {
    fprintf(stderr, "\nLibrary error, exiting...\n" );
  }

  if (output.filename!=0) {
    output.filename=0;
    CloseWaveOut(output.file);
  }
  
  SDL_CloseAudioDevice(_audio_device);

  if (_beverbose)
    fprintf(stderr, "Closing threads\n");

  // wait for dongle thread
  // rtlsdr_cancel_async must be called inside rtlsdr_callback thread
  pthread_join(dongle.thread, NULL); 
  safe_cond_signal(&demod.ready, &demod.ready_m);
  pthread_join(demod.thread, NULL);
  safe_cond_signal(&output.ready, &output.ready_m);
  pthread_join(output.thread, NULL);
  safe_cond_signal(&controller.hop, &controller.hop_m);
  pthread_join(controller.thread, NULL);

  if (_beverbose)
    fprintf(stderr, "Closing demodulator\n");
  demod_cleanup(&demod);
  if (_beverbose)
    fprintf(stderr, "Closing output\n");
  output_cleanup(&output);
  if (_beverbose)
    fprintf(stderr, "Closing controller\n");
  controller_cleanup(&controller);

  free(_circbuffer);

  if (_beverbose)
    fprintf(stderr, "Closing dongle\n");
  rtlsdr_close(dongle.dev);

  SDL_Quit();

  return librtlerr >= 0 ? librtlerr : -librtlerr;
}

