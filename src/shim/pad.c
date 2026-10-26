/* libpad: the NuPs2OpenPad / NuPs2ReadPad state machine in nups2/ps2ctl.c. Port 0 is a DualShock 2 in analog mode
   with pressure, already set up, so every request completes at once; port 1 has no controller. */
#include "shim/shim.h"

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "nups2/ps2ctl.h"

#define NW_PAD_PORTS 2
#define NW_PAD_DISCONNECTED 0
#define NW_PAD_STABLE 6
#define NW_PAD_NOT_OPEN 99
#define NW_PAD_DUALSHOCK 7
#define NW_PAD_ID_PRESSURE 0x79
#define NW_PAD_ACTUATORS 2

static SDL_Mutex *nw_pad_mutex;
static SDL_InitState nw_pad_init;
static NwPadInput nw_pad_input = { 0, 0x80, 0x80, 0x80, 0x80, 0, 0 };
static bool nw_pad_open[NW_PAD_PORTS];

static void nw_pad_lock(void)
{
    if (SDL_ShouldInit(&nw_pad_init)) {
        nw_pad_mutex = SDL_CreateMutex();
        SDL_SetInitialized(&nw_pad_init, nw_pad_mutex != NULL);
    }
    SDL_LockMutex(nw_pad_mutex);
}

static bool nw_pad_live(int port, int slot)
{
    return port == 0 && slot == 0 && nw_pad_open[0];
}

static uint8_t nw_pad_pressure(uint16_t buttons, uint16_t bit)
{
    return buttons & bit ? 0xFF : 0x00;
}

void nw_pad_set(const NwPadInput *in)
{
    nw_pad_lock();
    nw_pad_input = *in;
    SDL_UnlockMutex(nw_pad_mutex);
}

int scePadPortOpen(int port, int slot, void *addr)
{
    (void)addr;
    if (port < 0 || port >= NW_PAD_PORTS || slot != 0 || nw_pad_open[port]) {
        return 0;
    }
    nw_pad_open[port] = true;
    return 1;
}

int scePadPortClose(int port, int slot)
{
    if (port < 0 || port >= NW_PAD_PORTS || slot != 0 || !nw_pad_open[port]) {
        return 0;
    }
    nw_pad_open[port] = false;
    return 1;
}

int scePadRead(int port, int slot, struct nupadframe_s *data)
{
    if (!nw_pad_live(port, slot)) {
        return 0;
    }
    nw_pad_lock();
    NwPadInput in = nw_pad_input;
    SDL_UnlockMutex(nw_pad_mutex);
    uint16_t low = (uint16_t)~in.buttons;
    data->ok = 0;
    data->read_id = NW_PAD_ID_PRESSURE;
    data->buttons_hi = (uint8_t)(low >> 8);
    data->buttons_lo = (uint8_t)low;
    data->r_alg_x = in.rx;
    data->r_alg_y = in.ry;
    data->l_alg_x = in.lx;
    data->l_alg_y = in.ly;
    data->l_algpad_r = nw_pad_pressure(in.buttons, NW_PAD_RIGHT);
    data->l_algpad_l = nw_pad_pressure(in.buttons, NW_PAD_LEFT);
    data->l_algpad_u = nw_pad_pressure(in.buttons, NW_PAD_UP);
    data->l_algpad_d = nw_pad_pressure(in.buttons, NW_PAD_DOWN);
    data->r_algpad_u = nw_pad_pressure(in.buttons, NW_PAD_TRIANGLE);
    data->r_algpad_r = nw_pad_pressure(in.buttons, NW_PAD_CIRCLE);
    data->r_algpad_d = nw_pad_pressure(in.buttons, NW_PAD_CROSS);
    data->r_algpad_l = nw_pad_pressure(in.buttons, NW_PAD_SQUARE);
    data->l1_alg = nw_pad_pressure(in.buttons, NW_PAD_L1);
    data->r1_alg = nw_pad_pressure(in.buttons, NW_PAD_R1);
    data->l2_alg = in.l2;
    data->r2_alg = in.r2;
    return (int)sizeof(*data);
}

int scePadGetState(int port, int slot)
{
    if (port < 0 || port >= NW_PAD_PORTS || slot != 0 || !nw_pad_open[port]) {
        return NW_PAD_NOT_OPEN;
    }
    return port == 0 ? NW_PAD_STABLE : NW_PAD_DISCONNECTED;
}

/* 0: the last request completed. */
int scePadGetReqState(int port, int slot)
{
    (void)port, (void)slot;
    return 0;
}

int scePadInfoAct(int port, int slot, int actno, int term)
{
    (void)term;
    return nw_pad_live(port, slot) && actno == -1 ? NW_PAD_ACTUATORS : 0;
}

/* term 1 and 2: the current id and extended id; 3: the current mode's offset; 4: the mode count (offs -1) or the
   id of mode offs (digital 4, DualShock 7). */
int scePadInfoMode(int port, int slot, int term, int offs)
{
    if (!nw_pad_live(port, slot)) {
        return 0;
    }
    switch (term) {
    case 1:
    case 2:
        return NW_PAD_DUALSHOCK;
    case 3:
        return 1;
    case 4:
        return offs == -1 ? 2 : offs == 0 ? 4 : offs == 1 ? NW_PAD_DUALSHOCK : 0;
    default:
        return 0;
    }
}

int scePadSetMainMode(int port, int slot, int offs, int lock)
{
    (void)offs, (void)lock;
    return nw_pad_live(port, slot);
}

/* The motors stay off. */
int scePadSetActDirect(int port, int slot, unsigned char *data)
{
    (void)data;
    return nw_pad_live(port, slot);
}

int scePadSetActAlign(int port, int slot, unsigned char *data)
{
    (void)data;
    return nw_pad_live(port, slot);
}

int scePadInfoPressMode(int port, int slot)
{
    return nw_pad_live(port, slot);
}

int scePadEnterPressMode(int port, int slot)
{
    return nw_pad_live(port, slot);
}
