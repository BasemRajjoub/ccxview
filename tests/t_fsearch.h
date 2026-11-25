/* t_fsearch.h -- fuzzy feature search (fsearch.c). Included by test_main.c. */
#include "../src/fsearch.h"

static void test_fsearch(void) {
    CHECK(cv_fs_score("shadow", "Shadows", "", "") > cv_fs_score("shadow", "Lighting", "", "casts a shadow"));
    CHECK(cv_fs_score("shadow", "Lighting", "", "casts a shadow") > 0);
    CHECK(cv_fs_score("shadw", "Shadows", "", "") > 0);
    CHECK(cv_fs_score("shadw", "Shadows", "", "") < cv_fs_score("shadows", "Shadows", "", ""));
    CHECK(cv_fs_score("shdaow", "Shadow", NULL, NULL) > 0);      /* transposition */
    CHECK(cv_fs_score("legend pos", "Legend position", "", "") > 0);
    CHECK_EQ(cv_fs_score("legend zzz", "Legend position", "", ""), 0);
    CHECK(cv_fs_score("rv", "Render view", "", "") > 0);
    CHECK(cv_fs_score("aoc", "Ambient occlusion", "", "") > 0);
    CHECK(cv_fs_score("bg", "Colour", "background backdrop", "") > 0);
    CHECK_EQ(cv_fs_score("xyz", "Render view", "background", "some help"), 0);
    CHECK(cv_fs_score("", "Anything", "", "") > 0);
    CHECK(cv_fs_score("RENDER", "render view", "", "") > 0);

    const char* lab[] = { "Alpha", "Shadows", "Shadowed", "Beta" };
    const char* hlp[] = { "", "", "", "shadow here" };
    int ord[8];
    int c = cv_fs_rank("shadow", 4, lab, NULL, hlp, ord, 8);
    CHECK_EQ(c, 3);
    CHECK_EQ(ord[0], 1);                                          /* tie with 2: original order */
    CHECK_EQ(ord[1], 2);
    CHECK_EQ(ord[2], 3);
    c = cv_fs_rank("", 4, lab, NULL, NULL, ord, 8);
    CHECK_EQ(c, 4);
    CHECK(ord[0] == 0 && ord[1] == 1 && ord[2] == 2 && ord[3] == 3);
    c = cv_fs_rank("", 4, lab, NULL, NULL, ord, 2);
    CHECK_EQ(c, 2);
    CHECK_EQ(cv_fs_rank("xyz", 4, lab, NULL, NULL, ord, 8), 0);

    char big[2000], bigh[2000];
    memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
    memset(bigh, 'b', sizeof bigh - 1); bigh[sizeof bigh - 1] = 0;
    CHECK_EQ(cv_fs_score("zzz", big, big, bigh), 0);
    CHECK(cv_fs_score("bbb", "x", "", bigh) > 0);
    CHECK(cv_fs_score(big, big, big, bigh) >= 0);
}
