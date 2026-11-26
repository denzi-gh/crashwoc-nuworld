#include "script/bursts.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static uint32_t old_hash(uint32_t x) { x^=x>>16; x*=0x7FEB352Du; x^=x>>15; x*=0x846CA68Bu; x^=x>>16; return x; }
static bool old_on(int bolt, int frame, float chance)
{
    int slot=frame/90, at=frame%90; uint32_t h=old_hash((uint32_t)slot*2654435761u ^ (uint32_t)bolt*40503u);
    if (h%100 >= (uint32_t)chance) return false;
    at-=(int)((h>>8)%40); int flashes=2+(int)((h>>16)%4);
    for (int i=0; i<flashes && at>=0; i++) {
        uint32_t f=old_hash(h+(uint32_t)i); int on=2+(int)(f%3), off=2+(int)((f>>4)%4);
        if (at<on) return true;
        at-=on+off;
    }
    return false;
}
int main(void)
{
    const NwBurst recipe={.frames={0,1},.slot=90,.seed={2654435761u,40503},.delay={0,39},
        .flashes={2,5},.on={2,4},.off={2,5}};
    const NwBurst *b=&recipe;
    const float chances[]={0,.5f,1,15,55,99,100}; size_t checks=0;
    for (size_t c=0; c<sizeof(chances)/sizeof(*chances); c++) for (int channel=0; channel<8; channel++) {
        for (int tick=0; tick<12000; tick++) {
            bool on; CHECK(nw_burst_sample(b,tick,(uint32_t)channel,chances[c],0,&on) && on == old_on(channel,tick,chances[c])); checks++;
        }
        const int edges[]={16777215,16777216,16777217,INT_MAX-1,INT_MAX};
        for (size_t i=0; i<sizeof(edges)/sizeof(*edges); i++) {
            bool on; CHECK(nw_burst_sample(b,edges[i],(uint32_t)channel,chances[c],0,&on) && on == old_on(channel,edges[i],chances[c])); checks++;
        }
    }
    bool on;
    CHECK(!nw_burst_sample(b,-1,0,55,0,&on) && !nw_burst_sample(b,0,0,NAN,0,&on) && !nw_burst_sample(b,0,0,101,0,&on));
    NwBurst invalid=*b; invalid.on[0]=0; CHECK(!nw_burst_sample(&invalid,10,0,55,0,&on));
    invalid=*b; invalid.delay[1]=UINT32_MAX; CHECK(!nw_burst_sample(&invalid,10,0,55,0,&on));
    invalid=*b; invalid.slot=100000; invalid.delay[0]=invalid.delay[1]=0; invalid.flashes[0]=invalid.flashes[1]=10000;
    invalid.on[0]=invalid.on[1]=1; invalid.off[0]=invalid.off[1]=0;
    CHECK(!nw_burst_sample(&invalid,5000,0,100,16,&on));
    printf("bursts: %zu exact legacy comparisons passed\n",checks); return 0;
}
