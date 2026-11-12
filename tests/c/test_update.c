/* platform/update.c: the tag in GitHub's release JSON and the version order. */
#include <stdio.h>

#include <SDL3/SDL.h>

#include "platform/update.h"

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int order(const char *a, const char *b)
{
    int o = 99;
    return nw_update_compare(a, b, &o) ? o : 99;
}

int main(void)
{
    char tag[32];
    CHECK(nw_update_tag("{\"url\":\"x\",\"tag_name\":\"v0.8.0\",\"name\":\"v0.8.0\"}", tag, sizeof(tag)) &&
          SDL_strcmp(tag, "v0.8.0") == 0);
    CHECK(nw_update_tag("{\n  \"tag_name\": \"1.2\",\n  \"body\": \"\"\n}", tag, sizeof(tag)) &&
          SDL_strcmp(tag, "1.2") == 0);
    CHECK(!nw_update_tag("{\"tag_name\":\"build-20261112\"}", tag, sizeof(tag)));
    CHECK(!nw_update_tag("{\"tag_name\":\"v0.8.0/../x\"}", tag, sizeof(tag)));
    CHECK(!nw_update_tag("{\"tag_name\":\"v0.8.0", tag, sizeof(tag)));
    CHECK(!nw_update_tag("{\"tag_name\":\"v0.8.0\"}", tag, 4));
    CHECK(!nw_update_tag("{\"name\":\"v0.8.0\"}", tag, sizeof(tag)));

    CHECK(order("v0.8.0", "0.7.0") > 0);
    CHECK(order("0.7.0", "v0.7.0") == 0);
    CHECK(order("0.7.0", "0.8.0") < 0);
    CHECK(order("0.7.10", "0.7.9") > 0);
    CHECK(order("1", "0.9.9") > 0);
    CHECK(order("1.0", "1.0.0") == 0);
    CHECK(order("0.7.0-beta", "0.7.0") == 99);
    CHECK(order("1.2.3.4", "1.2.3") == 99);
    CHECK(order("", "1") == 99);
    CHECK(order("v", "1") == 99);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("update: ok\n");
    return 0;
}
