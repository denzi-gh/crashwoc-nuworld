/* MOVIES.TXT (docs/csp-spec.md, paragraph 5): which movies the PC system plays, by the number game/cut.c gives them,
   and the first cut of each; a movie is that cut and the chain of its next_cut_scene. A movie not listed stays the
   PS2 cutscenes'. */
#include "port/gcutpc/internal.h"
#include "port/gcutpc/nugcutpc_config.h"

static bool word_movie(NUGCUTPCTXT_s *txt, void *t)
{
    NUGCUTPCMOVIES_s *movies = t;
    int32_t number;
    if (!NuGCutPCTxtInt(txt, &number)) {
        return false;
    }
    if (NuGCutPCMovieCut(movies, number)) {
        return NuGCutPCTxtFail(txt, "movie %d twice", number);
    }
    NUGCUTPCMOVIE_s *movie = NUGCUTPC_PUSH(txt, movies->movies, movies->count);
    if (!movie) {
        return false;
    }
    movie->number = number;
    return NuGCutPCTxtString(txt, &movie->cut);
}

static const NUGCUTPCWORD_s movies_words[] = {
    { .name = "movie", .read = word_movie, .repeat = true },
};
static const NUGCUTPCVOCABULARY_s movies_vocabulary = { movies_words, 1 };

NUGCUTPCMOVIES_s *NuGCutPCMoviesRead(const char *text, size_t size, const char *where, NUGCUTPCERROR_s *error)
{
    NUGCUTPCERROR_s ignored;
    error = error ? error : &ignored;
    error->text[0] = '\0';
    NUGCUTPCARENA_s *arena = NuGCutPCArenaCreate();
    NUGCUTPCMOVIES_s *movies = arena ? NuGCutPCArenaAlloc(arena, 1, sizeof(*movies)) : NULL;
    if (!movies) {
        NuGCutPCArenaDestroy(arena);
        NuGCutPCFail(error, "out of memory");
        return NULL;
    }
    movies->arena = arena;
    NUGCUTPCTXT_s txt = { .text = text, .size = size, .where = where ? where : "MOVIES.TXT", .arena = arena,
                          .error = error };
    if (!NuGCutPCTxtRead(&txt, &movies_vocabulary, movies)) {
        NuGCutPCArenaDestroy(arena);
        return NULL;
    }
    return movies;
}

void NuGCutPCMoviesFree(NUGCUTPCMOVIES_s *movies)
{
    if (movies) {
        NuGCutPCArenaDestroy(movies->arena);
    }
}

const char *NuGCutPCMovieCut(const NUGCUTPCMOVIES_s *movies, int32_t number)
{
    for (uint32_t i = 0; i < movies->count; i++) {
        if (movies->movies[i].number == number) {
            return movies->movies[i].cut;
        }
    }
    return NULL;
}
