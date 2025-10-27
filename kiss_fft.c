
/* kiss_fft.c - KISS FFT portable C implementation */

#include "kiss_fft.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

struct kiss_fft_state {
    int nfft;
    int inverse;
    kiss_fft_cpx *twiddles;
};

kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void * mem, size_t * lenmem) {
    kiss_fft_cfg st = NULL;
    size_t memneeded = sizeof(struct kiss_fft_state) + sizeof(kiss_fft_cpx) * nfft;

    if (lenmem) {
        if (*lenmem >= memneeded)
            st = (kiss_fft_cfg)mem;
        *lenmem = memneeded;
    }
    if (!st)
        st = (kiss_fft_cfg)malloc(memneeded);

    if (!st) return NULL;

    st->nfft = nfft;
    st->inverse = inverse_fft;
    st->twiddles = (kiss_fft_cpx *)(st + 1);

    for (int i = 0; i < nfft; ++i) {
        double phase = -2 * M_PI * i / nfft;
        if (st->inverse) phase = -phase;
        st->twiddles[i].r = (kiss_fft_scalar)cos(phase);
        st->twiddles[i].i = (kiss_fft_scalar)sin(phase);
    }

    return st;
}

void kiss_fft(kiss_fft_cfg st, const kiss_fft_cpx *fin, kiss_fft_cpx *fout) {
    int k, n;
    int N = st->nfft;
    for (k = 0; k < N; ++k) {
        fout[k].r = fout[k].i = 0;
        for (n = 0; n < N; ++n) {
            double tw_r = st->twiddles[(k * n) % N].r;
            double tw_i = st->twiddles[(k * n) % N].i;
            double in_r = fin[n].r;
            double in_i = fin[n].i;
            fout[k].r += in_r * tw_r - in_i * tw_i;
            fout[k].i += in_r * tw_i + in_i * tw_r;
        }
        if (st->inverse) {
            fout[k].r /= N;
            fout[k].i /= N;
        }
    }
}

void kiss_fft_free(void *cfg) {
    free(cfg);
}
