/* The build ID testers name in reports (build/gen/buildid.c, cmake/buildid.cmake): the version, the release tag
   (NUWORLD_TAG, may be empty) and the commit, "-dirty" with local changes. */
#ifndef NUWORLD_PLATFORM_BUILDID_H
#define NUWORLD_PLATFORM_BUILDID_H

extern const char nw_build_version[];
extern const char nw_build_tag[];
extern const char nw_build_commit[];
extern const char nw_build_decomp[]; /* the decomp submodule's commit */
extern const char nw_build_id[];     /* "0.7.0 BETA a1b2c3d" */

#endif
