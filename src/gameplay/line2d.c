#include "gameplay/line2d.h"

#include "game/creature.h"
#include "game/gametypes.h"
#include "gameplay/gameplay.h"
#include "nu3d/nugscn.h"
#include "nu_port.h"

/* Declared by no header. */
extern struct rail_s Rail[];
extern f32 NuTrigTable[];

/* line_2d: back to the line by this share of the distance a frame, at most this speed. */
#define NW_LINE2D_PULL NW_HZ(0.25f, 0.213f)
#define NW_LINE2D_MAX NW_HZ(0.02f, 0.016666668f)

static bool nw_line2d_on;    /* in a side-on section */
static signed char nw_line2d_rail;
static float nw_line2d_across; /* where across the rail he entered it, 0 its left spline, 1 its right */

static struct nuvec_s *nw_line2d_point(const struct nugspline_s *s, int i)
{
    return (struct nuvec_s *)(s->pts + i * s->ptsize);
}

/* The rail's point at along and across, as BestRailPosition measures them. */
static struct nuvec_s nw_line2d_at(const RailPosition *r, float across)
{
    const struct rail_s *rail = &Rail[r->iRAIL];
    int i0 = r->iALONG, i1 = i0 + 1 == rail->edges && rail->circuit ? 0 : i0 + 1;
    const struct nuvec_s *l0 = nw_line2d_point(rail->pLEFT, i0), *l1 = nw_line2d_point(rail->pLEFT, i1);
    const struct nuvec_s *r0 = nw_line2d_point(rail->pRIGHT, i0), *r1 = nw_line2d_point(rail->pRIGHT, i1);
    float lx = l0->x + (l1->x - l0->x) * r->fALONG, lz = l0->z + (l1->z - l0->z) * r->fALONG;
    float rx = r0->x + (r1->x - r0->x) * r->fALONG, rz = r0->z + (r1->z - r0->z) * r->fALONG;
    return (struct nuvec_s){ lx + (rx - lx) * across, 0.0f, lz + (rz - lz) * across };
}

void nw_line2d_hold(struct creature_s *plr)
{
    /* RPos is where the last frame left him, which MoveLoopXZ's move starts from. */
    const RailPosition *r = &plr->obj.RPos;
    bool side = r->iRAIL != -1 && r->iALONG != -1 && (r->mode & 0xC) && !r->vertical;
    if (!nw_profile.movement.crash.line_2d || !side || plr->obj.dead || plr->obj.transporting || plr->obj.dangle) {
        nw_line2d_on = false;
        return;
    }
    NW_DEVIATION(movement.crash.line_2d);
    if (!nw_line2d_on || nw_line2d_rail != r->iRAIL) {
        nw_line2d_on = true;
        nw_line2d_rail = r->iRAIL;
        nw_line2d_across = r->fACROSS;
    }
    u16 a = (u16)(r->angle + 0x4000);
    float ax = NuTrigTable[a], az = NuTrigTable[(u16)(a + 0x4000)];
    float drift = plr->obj.mom.x * ax + plr->obj.mom.z * az;
    struct nuvec_s line = nw_line2d_at(r, nw_line2d_across);
    float off = (line.x - plr->obj.pos.x) * ax + (line.z - plr->obj.pos.z) * az, pull = off * NW_LINE2D_PULL;
    pull = pull > NW_LINE2D_MAX ? NW_LINE2D_MAX : pull < -NW_LINE2D_MAX ? -NW_LINE2D_MAX : pull;
    plr->obj.mom.x += (pull - drift) * ax;
    plr->obj.mom.z += (pull - drift) * az;
}
