#include "gfx/mtl.h"

#include "gfx/gsc.h"

/* NuTexWriteMaterialSettings' tables by attrib.filter and attrib.utc / vtc (retail 0x2D3F20, 0x2D3F30, 0x2D3F40). */
static const uint8_t nw_mtl_wrap[4] = { 0, 1, 0, 0 };
static const uint8_t nw_mtl_mmag[4] = { 0, 1, 1, 0 };
static const uint8_t nw_mtl_mmin[4] = { 0, 1, 5, 0 };

NwMtlState nw_mtl_state(const uint32_t attrib[2])
{
    NwMtlState s;
    s.mmag = nw_mtl_mmag[NW_GSC_ATTRIB_FILTER(attrib)];
    s.mmin = nw_mtl_mmin[NW_GSC_ATTRIB_FILTER(attrib)];
    s.wms = nw_mtl_wrap[NW_GSC_ATTRIB_UTC(attrib)];
    s.wmt = nw_mtl_wrap[NW_GSC_ATTRIB_VTC(attrib)];
    return s;
}
