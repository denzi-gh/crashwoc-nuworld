/* The pad shim under the decomp's NuPs2OpenPad / NuPs2ReadPad, a virtual SDL gamepad and the keyboard as the pad,
   and the pad scripts (port/padscript.h). */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "nups2/ps2ctl.h"
#include "platform/input.h"
#include "port/padscript.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static const NwPadInput idle = { 0, 0x80, 0x80, 0x80, 0x80, 0, 0 };

/* NuPs2ReadPad steps its setup once per call; a DualShock 2 is ready well within 16. */
static void read_until_valid(struct nupad_s *pad)
{
    for (int i = 0; i < 16 && !pad->valid; i++) {
        NuPs2ReadPad(pad);
    }
}

static void test_shim(void)
{
    nw_pad_set(&idle);
    struct nupad_s *pad = NuPs2OpenPad(0, 0);
    CHECK(pad != NULL);
    if (!pad) {
        return;
    }
    read_until_valid(pad);
    CHECK(pad->valid && pad->analogue_sticks && pad->analogue_buttons);
    CHECK(pad->rdata.read_id == 0x79);
    CHECK(pad->paddata == 0 && pad->ldx == 0 && pad->ldy == 0);

    NwPadInput in = { NW_PAD_CROSS | NW_PAD_L2, 0xFF, 0x00, 0x80, 0x80, 0xFF, 0 };
    nw_pad_set(&in);
    NuPs2ReadPad(pad);
    CHECK(pad->paddata == (NW_PAD_CROSS | NW_PAD_L2));
    CHECK(pad->paddata_db == (NW_PAD_CROSS | NW_PAD_L2));
    CHECK(pad->rdata.l_alg_x == 0xFF && pad->rdata.l_alg_y == 0x00);
    CHECK(pad->ldx == 0x7F && pad->ldy == 0x80);
    CHECK(pad->rdata.r_algpad_d == 0xFF && pad->rdata.r_algpad_l == 0 && pad->rdata.l2_alg == 0xFF);
    NuPs2ReadPad(pad);
    CHECK(pad->paddata == (NW_PAD_CROSS | NW_PAD_L2) && pad->paddata_db == 0);
    nw_pad_set(&idle);
    NuPs2ReadPad(pad);
    CHECK(pad->valid && pad->paddata == 0 && pad->ldx == 0);
    NuPs2PadSetMotors(pad, 1, 200);

    struct nupad_s *second = NuPs2OpenPad(1, 0);
    CHECK(second != NULL);
    if (second) {
        read_until_valid(second);
        CHECK(!second->valid && second->paddata == 0);
        NuPs2PadSetMotors(second, 1, 200);
    }
}

static void test_gamepad(void)
{
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "test_pad: SDL_Init: %s\n", SDL_GetError());
        failures++;
        return;
    }
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "nuworld test pad";
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Gamepad *gamepad = id ? SDL_OpenGamepad(id) : NULL;
    CHECK(gamepad != NULL);
    if (!gamepad) {
        fprintf(stderr, "test_pad: virtual gamepad: %s\n", SDL_GetError());
        SDL_Quit();
        return;
    }
    SDL_Joystick *joystick = SDL_GetGamepadJoystick(gamepad);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_WEST, true);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, true);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 0);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, 0);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
    SDL_UpdateGamepads();

    NwPadInput in;
    nw_input_gamepad(gamepad, &in);
    CHECK(in.buttons == (NW_PAD_CROSS | NW_PAD_SQUARE | NW_PAD_START | NW_PAD_LEFT | NW_PAD_R2));
    CHECK(in.lx == 0xFF && in.ly == 0x00 && in.rx == 0x80 && in.ry == 0x80);
    CHECK(in.l2 == 0x00 && in.r2 == 0xFF);

    SDL_CloseGamepad(gamepad);
    SDL_DetachVirtualJoystick(id);
    SDL_Quit();
}

static void test_keyboard(void)
{
    static bool keys[SDL_SCANCODE_COUNT];
    NwPadInput in = idle;
    keys[SDL_SCANCODE_SPACE] = true;
    nw_input_keyboard(keys, &in);
    CHECK(in.buttons == NW_PAD_CROSS && in.lx == 0x80 && in.ly == 0x80);

    keys[SDL_SCANCODE_SPACE] = false;
    keys[SDL_SCANCODE_W] = keys[SDL_SCANCODE_D] = keys[SDL_SCANCODE_3] = true;
    in = idle;
    nw_input_keyboard(keys, &in);
    CHECK(in.buttons == NW_PAD_R2 && in.r2 == 0xFF && in.l2 == 0);
    CHECK(in.lx == 0xFF && in.ly == 0x00);

    /* With a gamepad: the buttons add up, the pushed key axis wins, the other stays the gamepad's. */
    keys[SDL_SCANCODE_W] = keys[SDL_SCANCODE_D] = keys[SDL_SCANCODE_3] = false;
    keys[SDL_SCANCODE_A] = keys[SDL_SCANCODE_RETURN] = true;
    in = (NwPadInput){ NW_PAD_SQUARE, 0xC0, 0x40, 0x80, 0x80, 0, 0 };
    nw_input_keyboard(keys, &in);
    CHECK(in.buttons == (NW_PAD_SQUARE | NW_PAD_START));
    CHECK(in.lx == 0x00 && in.ly == 0x40);

    /* Opposite keys cancel out and leave the gamepad's axis. */
    keys[SDL_SCANCODE_D] = true;
    in = (NwPadInput){ 0, 0xC0, 0x40, 0x80, 0x80, 0, 0 };
    nw_input_keyboard(keys, &in);
    CHECK(in.lx == 0xC0);
}

/* --pad-script without --level: the steps loop from the first read; a recording plays once, then the pad idles. */
static void test_script(void)
{
    struct nupadframe_s f;
    CHECK(!nw_pad_script_read(&f));
    CHECK(!nw_pad_script_parse("x cross\n"));
    CHECK(!nw_pad_script_parse("1 jump\n"));
    CHECK(!nw_pad_script_parse("1 cross 1 2 3\n"));
    CHECK(!nw_pad_script_parse("# nothing\n"));

    CHECK(nw_pad_script_parse("2 cross|L2 255 0 # comment\n\n1 -\n"));
    for (int i = 0; i < 2; i++) {
        CHECK(nw_pad_script_read(&f));
        CHECK(f.buttons_hi == 0xFF && f.buttons_lo == (uint8_t)~(NW_PAD_CROSS | NW_PAD_L2));
        CHECK(f.l_alg_x == 0xFF && f.l_alg_y == 0x00 && f.r_alg_x == 0x80 && f.r_algpad_d == 0xFF);
        CHECK(f.l2_alg == 0xFF && f.r2_alg == 0);
    }
    CHECK(nw_pad_script_read(&f) && f.buttons_hi == 0xFF && f.buttons_lo == 0xFF && f.l_alg_x == 0x80);
    CHECK(nw_pad_script_read(&f) && f.buttons_lo == (uint8_t)~(NW_PAD_CROSS | NW_PAD_L2));

    /* struct nupadrec_s: frame, mode, nframes, maxframes, then the frames; SavePadRecord writes two past nframes. */
    uint8_t rec[0x10 + 4 * sizeof(struct nupadframe_s)] = { 0 };
    rec[8] = 2;
    struct nupadframe_s *frames = (struct nupadframe_s *)(rec + 0x10);
    frames[0] = (struct nupadframe_s){ .read_id = 0x79, .buttons_hi = 0xEF, .buttons_lo = 0xBF, .l_alg_x = 0x10 };
    frames[1] = (struct nupadframe_s){ .read_id = 0x79, .buttons_hi = 0, .buttons_lo = 0 };
    CHECK(!nw_pad_script_recording(rec, 0x0C));
    CHECK(nw_pad_script_recording(rec, sizeof(rec)));
    CHECK(nw_pad_script_read(&f) && f.buttons_hi == 0xEF && f.buttons_lo == 0xBF && f.l_alg_x == 0x10);
    CHECK(nw_pad_script_read(&f) && f.buttons_hi == 0xFF && f.buttons_lo == 0xFF && f.l_alg_x == 0x80);
    CHECK(nw_pad_script_read(&f) && f.buttons_hi == 0xFF && f.buttons_lo == 0xFF);
    rec[8] = 9;
    CHECK(!nw_pad_script_recording(rec, sizeof(rec)));
    nw_pad_script_stop();
    CHECK(!nw_pad_script_read(&f));
}

int main(void)
{
    test_shim();
    test_gamepad();
    test_keyboard();
    test_script();
    if (failures) {
        fprintf(stderr, "test_pad: %d check(s) failed\n", failures);
        return 1;
    }
    printf("test_pad: all checks passed\n");
    return 0;
}
