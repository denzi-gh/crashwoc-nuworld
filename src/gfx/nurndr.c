/* nu3d/nurndr, which the port overrides whole (port/units.toml): the frame bracket. Drawing belongs to the
   renderer (M3): every other NuRndr* is a stub, and their hits are its worklist. */
int NuPs2ScreenSwap(int vsync);
void NuPs2DisableAutoScreenSwap(void);

/* The data image root main.c reads. */
extern int nurndr_tritot_this_frame;

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
