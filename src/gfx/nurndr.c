/* nu3d/nurndr, which the port overrides whole (port/units.toml): the frame bracket. Drawing belongs to the
   renderer (M3): every other NuRndr* is a stub, and their hits are its worklist. */
int NuPs2ScreenSwap(int vsync);
void NuPs2DisableAutoScreenSwap(void);

struct nugscn_s;
struct nugspline_s;
int NuSplineFindAllSub(struct nugscn_s *scene, char *name, struct nugspline_s **list, int max);

/* The data image root main.c reads. */
extern int nurndr_tritot_this_frame;

/* Shadow masks: scene splines named like "stencil" (D_0062ECA8); only nurndr reads them. */
int NuRndrShadMaskCount;
struct nugspline_s *NuRndrShadMasks[32];
static char nw_stencil[] = "stencil";

void NuRndrSplineSearch(void *spline)
{
    int n = NuRndrShadMaskCount;
    NuRndrShadMaskCount += NuSplineFindAllSub(spline, nw_stencil, &NuRndrShadMasks[n], 32 - n);
}

/* Retail links the system stream and sets the target, Z buffer, offset and scissor: no stream yet. */
int NuRndrBeginScene(int target)
{
    (void)target;
    return 1;
}

/* Retail builds the material render list and flushes the ordering table and the stream: nothing was drawn. */
void NuRndrEndScene(void)
{
}

/* Target -3 swaps without counting a frame. Retail then kicks VU1, swaps the stream buffers and resets the frame
   counters; only the one main.c reads is kept. */
int NuRndrSwapScreen(int target)
{
    NuPs2DisableAutoScreenSwap();
    int swapped = NuPs2ScreenSwap(target != -3);
    nurndr_tritot_this_frame = 0;
    return swapped;
}
