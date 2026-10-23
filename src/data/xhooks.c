/* Hand-written converters for what port/convert.toml marks `hook = true`. */
#include "nw_convert.h"

/* The animation's per-curve flags decide between the constant and the curve data: the .cut loader converts
   these. */
void nw_xh_nuanimcurve2_u(union nuanimcurve2_u *dst, const nw_ps2_nuanimcurve2_u *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    nw_xfatal(ctx, NULL, "needs its curve flags (the .cut loader converts it)");
}

void nw_xhr_nuanimcurve2_u(nw_ps2_nuanimcurve2_u *dst, const union nuanimcurve2_u *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    nw_xfatal(ctx, NULL, "needs its curve flags (the .cut loader converts it)");
}

/* An open .DAT archive's directory: built when the archive opens, never in the retail data image. */
static _Noreturn void dat_directory(NwXCtx *ctx)
{
    nw_xfatal(ctx, "data", "a .DAT directory, built when its archive opens");
}

void nw_xh_nudathdr_s__data(struct nudathdr_s *dst, const nw_ps2_nudathdr_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    dat_directory(ctx);
}

void nw_xhr_nudathdr_s__data(nw_ps2_nudathdr_s *dst, const struct nudathdr_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    dat_directory(ctx);
}

/* The editor builds its menus at run time; none is in the retail data image. */
static _Noreturn void editor_only(NwXCtx *ctx, const char *site)
{
    nw_xfatal(ctx, site, "editor-only, not in the retail data image");
}

void nw_xh_eduiitem_s__1(struct eduiitem_s *dst, const nw_ps2_eduiitem_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    editor_only(ctx, "data");
}

void nw_xhr_eduiitem_s__1(nw_ps2_eduiitem_s *dst, const struct eduiitem_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    editor_only(ctx, "data");
}

void nw_xh_eduiitem_s__2(struct eduiitem_s *dst, const nw_ps2_eduiitem_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    editor_only(ctx, "u");
}

void nw_xhr_eduiitem_s__2(nw_ps2_eduiitem_s *dst, const struct eduiitem_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    editor_only(ctx, "u");
}

void nw_xh_eduimenu_s__titlebuf(struct eduimenu_s *dst, const nw_ps2_eduimenu_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    editor_only(ctx, "titlebuf");
}

void nw_xhr_eduimenu_s__titlebuf(nw_ps2_eduimenu_s *dst, const struct eduimenu_s *src, NwXCtx *ctx)
{
    (void)dst, (void)src;
    editor_only(ctx, "titlebuf");
}
