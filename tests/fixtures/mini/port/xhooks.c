/* Hooks for the fixture's port/convert.toml. */
#include "nw_convert.h"

void nw_xh_fixhook_u(union fixhook_u *dst, const nw_ps2_fixhook_u *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    nw_xfatal(ctx, NULL, "fixture hook");
}

void nw_xhr_fixhook_u(nw_ps2_fixhook_u *dst, const union fixhook_u *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    nw_xfatal(ctx, NULL, "fixture hook");
}

/* The tail has no bytes of its own. */
void nw_xh_fixunion_s__tail(struct fixunion_s *dst, const nw_ps2_fixunion_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src, (void)ctx;
}

void nw_xhr_fixunion_s__tail(nw_ps2_fixunion_s *dst, const struct fixunion_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src, (void)ctx;
}
