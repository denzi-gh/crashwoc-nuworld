/* What the .ani and animlib tests check on a loaded anim v1: its counts, and every curve at every frame. */
#ifndef NUWORLD_TEST_ANIM_CHECK_H
#define NUWORLD_TEST_ANIM_CHECK_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "nu3d/nuanim.h"

/* Declared by no header. */
void NuAnimDataCalcTime(struct nuanimdata_s *data, float time, struct nuanimtime_s *atime);

typedef struct AnimStats {
    unsigned anims, chunks, sets, curves, keys;
    int minsets;                      /* the fewest curves in a set: the appliers read set[0..8] unchecked */
    unsigned before, past, nonfinite; /* reads before a curve's first key, past its last; inf or NaN values */
    unsigned infdt;                   /* reads of a 1/0 dtime (two keys on one frame) that give the key's value */
    unsigned bad;                    /* reads outside the file */
} AnimStats;

#define ANIM_STATS_INIT { .minsets = 127 }

static void anim_add(AnimStats *to, const AnimStats *from)
{
    to->anims += from->anims;
    to->chunks += from->chunks;
    to->sets += from->sets;
    to->curves += from->curves;
    to->keys += from->keys;
    to->minsets = from->minsets < to->minsets ? from->minsets : to->minsets;
    to->before += from->before;
    to->past += from->past;
    to->nonfinite += from->nonfinite;
    to->infdt += from->infdt;
    to->bad += from->bad;
}

static void anim_print(const char *what, const AnimStats *st)
{
    printf("%s anims=%u chunks=%u sets=%u curves=%u keys=%u minsets=%d before=%u past=%u nonfinite=%u infdt=%u "
           "bad=%u\n",
           what, st->anims, st->chunks, st->sets, st->curves, st->keys, st->minsets, st->before, st->past,
           st->nonfinite, st->infdt, st->bad);
}

/* Counts data into st, then evaluates every curve at every frame once the keys it reads are known to lie in
   [lo, hi); a frame before a curve's first key or past its last reads the file bytes next to them, as on the PS2. */
static void anim_check(AnimStats *st, const char *path, const char *lo, const char *hi, struct nuanimdata_s *data)
{
    st->anims++;
    for (int i = 0; data->chunks && i < data->nchunks; i++) {
        struct nuanimdatachunk_s *chunk = data->chunks[i];
        st->chunks += chunk != NULL;
        for (int j = 0; chunk && chunk->animcurvesets && j < chunk->numnodes; j++) {
            struct nuanimcurveset_s *set = chunk->animcurvesets[j];
            if (!set) {
                continue;
            }
            st->sets++;
            st->minsets = set->ncurves < st->minsets ? set->ncurves : st->minsets;
            for (int k = 0; set->set && k < set->ncurves; k++) {
                st->curves += set->set[k] != NULL;
                st->keys += set->set[k] ? set->set[k]->numkeys : 0;
            }
        }
    }
    for (float t = 1.0f; data->chunks && data->nchunks > 0 && t <= data->time + 1.0f; t += 1.0f) {
        struct nuanimtime_s at;
        NuAnimDataCalcTime(data, t, &at);
        struct nuanimdatachunk_s *chunk = data->chunks[at.chunk];
        int key = (int)floorf(at.time_offset) - 1;
        if (key < 0 || key > 31) {
            st->bad++;
            continue;
        }
        uint32_t upto = (uint32_t)((2ull << key) - 1);
        for (int j = 0; chunk && chunk->animcurvesets && j < chunk->numnodes; j++) {
            struct nuanimcurveset_s *set = chunk->animcurvesets[j];
            for (int k = 0; set && set->set && k < set->ncurves; k++) {
                struct nuanimcurve_s *c = set->set[k];
                if (!c) {
                    continue;
                }
                int ix = __builtin_popcount(c->mask.bits & upto) - 1, last = c->flags & 1 ? ix : ix + 1;
                const char *first = (const char *)c->animkeys + ix * 16, *end = (const char *)c->animkeys + last * 16 + 16;
                st->before += ix < 0;
                st->past += last >= (int)c->numkeys;
                if (first < lo || end > hi) {
                    if (st->bad++ < 10) {
                        fprintf(stderr, "%s: chunk %d node %d curve %d at frame %g reads outside the file\n", path,
                                at.chunk, j, k, t);
                    }
                    continue;
                }
                float v = NuAnimCurveCalcVal2(c, &at);
                st->nonfinite += !isfinite(v);
                st->infdt += !(c->flags & 1) && isinf(c->animkeys[ix].dtime) && v == c->animkeys[ix].d;
            }
        }
    }
}

#endif
