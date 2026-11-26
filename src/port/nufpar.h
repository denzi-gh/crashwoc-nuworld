#ifndef NUWORLD_PORT_NUFPAR_H
#define NUWORLD_PORT_NUFPAR_H
#include "nucore/nufpar.h"

/* Legacy fields stay compatible. Read extended text through these accessors. */
int NuFParSetLimits(struct nufpar_s *parser, int line, int word);
const char *NuFParGetError(const struct nufpar_s *parser);
const char *NuFParLine(const struct nufpar_s *parser);
const char *NuFParWord(const struct nufpar_s *parser);
struct nufpar_s *NuFParOpenSized(int handle, int size); /* Known-size streams; caller retains handle on failure. */
#endif
