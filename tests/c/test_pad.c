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
    uint8_t small, big;
    NuPs2PadSetMotors(pad, 3, 200);
    nw_pad_motors(&small, &big);
    CHECK(small == 1 && big == 200);

    struct nupad_s *second = NuPs2OpenPad(1, 0);
    CHECK(second != NULL);
    if (second) {
        read_until_valid(second);
        CHECK(!second->valid && second->paddata == 0);
        NuPs2PadSetMotors(second, 0, 0);
        nw_pad_motors(&small, &big);
        CHECK(small == 1 && big == 200);
    }
}

static Uint16 rumble_low, rumble_high;

static bool rumble(void *user, Uint16 low, Uint16 high)
{
    (void)user;
    rumble_low = low;
    rumble_high = high;
    return true;
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
    desc.Rumble = rumble;
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

    NwControls c;
    nw_controls_defaults(&c);
    NwPadInput in;
    nw_input_gamepad(gamepad, &c, &in);
    CHECK(in.buttons == (NW_PAD_CROSS | NW_PAD_SQUARE | NW_PAD_START | NW_PAD_LEFT | NW_PAD_R2));
    CHECK(in.lx == 0xFF && in.ly == 0x00 && in.rx == 0x80 && in.ry == 0x80);
    CHECK(in.l2 == 0x00 && in.r2 == 0xFF);

    /* Remapped: the right trigger is cross (and off R2), L2 is south at full pressure, square has north too. */
    nw_controls_bind(c.pad, NW_CONTROL_BUTTONS, 0, NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    nw_controls_bind(c.pad, NW_CONTROL_BUTTONS, 6, 1 + SDL_GAMEPAD_BUTTON_SOUTH);
    c.pad[2][1] = 1 + SDL_GAMEPAD_BUTTON_NORTH;
    CHECK(c.pad[7][0] == 0 && c.pad[0][1] == 0);
    nw_input_gamepad(gamepad, &c, &in);
    CHECK(in.buttons == (NW_PAD_CROSS | NW_PAD_L2 | NW_PAD_SQUARE | NW_PAD_START | NW_PAD_LEFT));
    CHECK(in.l2 == 0xFF && in.r2 == 0x00);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_WEST, false);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_NORTH, true);
    SDL_UpdateGamepads();
    nw_input_gamepad(gamepad, &c, &in);
    CHECK(in.buttons & NW_PAD_SQUARE);

    /* Deadzones: inside one a stick is centred, outside it stretches to the full reach; the press point moves. */
    nw_controls_defaults(&c);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 6000);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, 0);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 0);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, SDL_JOYSTICK_AXIS_MAX);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0); /* half way */
    SDL_UpdateGamepads();
    nw_input_gamepad(gamepad, &c, &in);
    CHECK(in.lx == (6000 + 32768) >> 8 && in.ly == 0x80 && in.ry == 0xFF);
    CHECK(in.r2 >= 0x7F && in.r2 <= 0x80 && (in.buttons & NW_PAD_R2));
    c.left_deadzone = c.right_deadzone = 20;
    c.trigger_press = 60;
    nw_input_gamepad(gamepad, &c, &in);
    CHECK(in.lx == 0x80 && in.ly == 0x80 && in.rx == 0x80 && in.ry == 0xFF);
    CHECK(in.r2 >= 0x7F && in.r2 <= 0x80 && !(in.buttons & NW_PAD_R2));
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 16384);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, 0);
    SDL_UpdateGamepads();
    nw_input_gamepad(gamepad, &c, &in);
    CHECK(in.lx > 0xA0 && in.lx < 0xC0 && in.ry == 0x80); /* (0.5 - 0.2) / 0.8 of the reach */

    /* A listen hears the open gamepad's next button or trigger; Escape cancels. */
    SDL_Event e = { .gdevice = { .type = SDL_EVENT_GAMEPAD_ADDED, .which = id } };
    nw_input_event(&e);
    CHECK(nw_input_pad_name() != NULL && SDL_strcmp(nw_input_pad_label(1 + SDL_GAMEPAD_BUTTON_SOUTH), "A") == 0);
    CHECK(nw_input_heard() == -1);
    nw_input_listen(NW_INPUT_LISTEN_PAD);
    e = (SDL_Event){ .gbutton = { .type = SDL_EVENT_GAMEPAD_BUTTON_DOWN, .which = id,
                                  .button = SDL_GAMEPAD_BUTTON_EAST } };
    nw_input_event(&e);
    CHECK(nw_input_heard() == 1 + SDL_GAMEPAD_BUTTON_EAST && nw_input_heard() == -1);
    nw_input_event(&e);
    CHECK(nw_input_heard() == -1);
    nw_input_listen(NW_INPUT_LISTEN_PAD);
    e = (SDL_Event){ .gaxis = { .type = SDL_EVENT_GAMEPAD_AXIS_MOTION, .which = id,
                                .axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER, .value = SDL_JOYSTICK_AXIS_MAX } };
    nw_input_event(&e);
    CHECK(nw_input_heard() == NW_CONTROL_TRIGGER + SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    nw_input_listen(NW_INPUT_LISTEN_PAD);
    e = (SDL_Event){ .key = { .type = SDL_EVENT_KEY_DOWN, .scancode = SDL_SCANCODE_ESCAPE } };
    nw_input_event(&e);
    CHECK(nw_input_heard() == 0);

    /* The motors test_shim left on (small, big 200) rumble the gamepad at the rumble setting's strength. */
    nw_controls_defaults(&c);
    c.rumble = 50;
    nw_input_set_controls(&c);
    nw_input_poll(false);
    CHECK(rumble_low == 200 * 0x101 / 2 && rumble_high == 0xFFFF / 2);
    c.rumble = 0;
    nw_input_set_controls(&c);
    nw_input_poll(false);
    CHECK(rumble_low == 0 && rumble_high == 0);
    nw_input_shutdown();

    SDL_CloseGamepad(gamepad);
    SDL_DetachVirtualJoystick(id);
    SDL_Quit();
}

static void test_keyboard(void)
{
    static bool keys[SDL_SCANCODE_COUNT];
    NwControls c;
    nw_controls_defaults(&c);
    NwPadInput in = idle;
    keys[SDL_SCANCODE_SPACE] = true;
    nw_input_keyboard(keys, &c, &in);
    CHECK(in.buttons == NW_PAD_CROSS && in.lx == 0x80 && in.ly == 0x80);

    keys[SDL_SCANCODE_SPACE] = false;
    keys[SDL_SCANCODE_W] = keys[SDL_SCANCODE_D] = keys[SDL_SCANCODE_3] = true;
    in = idle;
    nw_input_keyboard(keys, &c, &in);
    CHECK(in.buttons == NW_PAD_R2 && in.r2 == 0xFF && in.l2 == 0);
    CHECK(in.lx == 0xFF && in.ly == 0x00);

    /* With a gamepad: the buttons add up, the pushed key axis wins, the other stays the gamepad's. */
    keys[SDL_SCANCODE_W] = keys[SDL_SCANCODE_D] = keys[SDL_SCANCODE_3] = false;
    keys[SDL_SCANCODE_A] = keys[SDL_SCANCODE_RETURN] = true;
    in = (NwPadInput){ NW_PAD_SQUARE, 0xC0, 0x40, 0x80, 0x80, 0, 0 };
    nw_input_keyboard(keys, &c, &in);
    CHECK(in.buttons == (NW_PAD_SQUARE | NW_PAD_START));
    CHECK(in.lx == 0x00 && in.ly == 0x40);

    /* Opposite keys cancel out and leave the gamepad's axis. */
    keys[SDL_SCANCODE_D] = true;
    in = (NwPadInput){ 0, 0xC0, 0x40, 0x80, 0x80, 0, 0 };
    nw_input_keyboard(keys, &c, &in);
    CHECK(in.lx == 0xC0);

    /* Remapped: Space is triangle and off cross, L the right stick's right, Escape alone is no longer start. */
    SDL_zeroa(keys);
    nw_controls_bind(c.keys, NW_CONTROL_TARGETS, 3, SDL_SCANCODE_SPACE);
    nw_controls_bind(c.keys, NW_CONTROL_TARGETS, NW_CONTROL_STICKS + 7, SDL_SCANCODE_L);
    nw_controls_bind(c.keys, NW_CONTROL_TARGETS, 11, SDL_SCANCODE_P);
    CHECK(c.keys[0][0] == 0 && c.keys[3][0] == SDL_SCANCODE_SPACE && c.keys[3][1] == 0);
    keys[SDL_SCANCODE_SPACE] = keys[SDL_SCANCODE_L] = keys[SDL_SCANCODE_ESCAPE] = keys[SDL_SCANCODE_W] = true;
    in = idle;
    nw_input_keyboard(keys, &c, &in);
    CHECK(in.buttons == NW_PAD_TRIANGLE && in.rx == 0xFF && in.ry == 0x80 && in.ly == 0x00);
    keys[SDL_SCANCODE_P] = true;
    nw_input_keyboard(keys, &c, &in);
    CHECK(in.buttons == (NW_PAD_TRIANGLE | NW_PAD_START));

    /* A key listen hears keys only, not their repeats; a gamepad listen no keys but Escape. */
    nw_input_listen(NW_INPUT_LISTEN_KEYS);
    SDL_Event e = { .key = { .type = SDL_EVENT_KEY_DOWN, .scancode = SDL_SCANCODE_Q, .repeat = true } };
    nw_input_event(&e);
    CHECK(nw_input_heard() == -1);
    e.key.repeat = false;
    nw_input_event(&e);
    CHECK(nw_input_heard() == SDL_SCANCODE_Q);
    nw_input_listen(NW_INPUT_LISTEN_PAD);
    nw_input_event(&e);
    CHECK(nw_input_heard() == -1);
    nw_input_listen(NW_INPUT_LISTEN_OFF);
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
