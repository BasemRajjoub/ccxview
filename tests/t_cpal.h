/* t_cpal.h -- the user's colour palettes (src/cpal.c): colours, the ini line, sampling,
   JSON and XML import (the shapes of #53), export and back */
#include "../src/cpal.h"

static void test_cpal(void) {
    uint8_t c[3];
    CHECK(cv_pal_color("211,211,211,255,LightGray", c) && c[0] == 211 && c[1] == 211 && c[2] == 211);
    CHECK(cv_pal_color(" 0, 92 ,255", c) && c[0] == 0 && c[1] == 92 && c[2] == 255);
    CHECK(cv_pal_color("#FF5c00", c) && c[0] == 255 && c[1] == 92 && c[2] == 0);
    CHECK(cv_pal_color("300,-4,7", c) && c[0] == 255 && c[1] == 0 && c[2] == 7);
    CHECK(!cv_pal_color("red", c));
    CHECK(!cv_pal_color("1,2", c));
    CHECK(!cv_pal_color("#12345", c));

    /* the ini line, both ways */
    cv_pal p;
    CHECK(cv_pal_parse("bands|#000000 #ff0000 0,0,255", &p));
    CHECK_EQ(p.n, 3); CHECK(!p.blend);
    CHECK(p.rgb[1][0] == 255 && p.rgb[2][2] == 255);
    CHECK(cv_pal_parse("blend|#000000;#ffffff", &p) && p.blend && p.n == 2);
    CHECK(!cv_pal_parse("bands|#000000", &p));
    char line[1024];
    cv_pal_parse("bands|#d3d3d3 #005cff #ff0000", &p);
    cv_pal_format(&p, line, sizeof line);
    CHECK(!strcmp(line, "bands|#d3d3d3 #005cff #ff0000"));
    cv_pal big;
    memset(&big, 0, sizeof big);
    big.n = CV_PAL_COLS;
    big.blend = true;
    cv_pal_format(&big, line, sizeof line);             /* the longest fits one ini value */
    CHECK(strlen(line) < 960);
    cv_pal q;
    CHECK(cv_pal_parse(line, &q) && q.n == CV_PAL_COLS && q.blend);

    /* sampling: a band per colour, or linear between them */
    float o[3];
    p.blend = false;
    cv_pal_sample(&p, 0.f, o);  CHECK_NEAR(o[0], 211 / 255.0, 1e-6);
    cv_pal_sample(&p, 0.34f, o); CHECK_NEAR(o[2], 1.0, 1e-6);
    cv_pal_sample(&p, 0.999f, o); CHECK_NEAR(o[0], 1.0, 1e-6);
    cv_pal_sample(&p, 1.f, o);  CHECK_NEAR(o[0], 1.0, 1e-6);
    for (int k = 0; k < p.n; k++) {                      /* the band centres, as the legend asks */
        cv_pal_sample(&p, ((float)k + 0.5f) / (float)p.n, o);
        CHECK_NEAR(o[1], p.rgb[k][1] / 255.0, 1e-6);
    }
    cv_pal_parse("blend|#000000 #ffffff", &p);
    cv_pal_sample(&p, 0.25f, o); CHECK_NEAR(o[0], 0.25, 1e-6);
    cv_pal_sample(&p, 2.f, o);   CHECK_NEAR(o[0], 1.0, 1e-6);

    /* the list: same name replaced, unique names, rename, move, remove */
    cv_pal_clear();
    unsigned g = cv_pal_gen();
    snprintf(p.name, sizeof p.name, "mine");
    CHECK_EQ(cv_pal_add(&p), 0);
    CHECK(cv_pal_gen() != g);
    snprintf(p.name, sizeof p.name, "a|b");             /* the ini separator never in a name */
    CHECK_EQ(cv_pal_add(&p), 1);
    CHECK(!strcmp(cv_pal_at(1)->name, "a/b"));
    p.blend = false;
    snprintf(p.name, sizeof p.name, "mine");
    CHECK_EQ(cv_pal_add(&p), 0);
    CHECK_EQ(cv_pal_count(), 2);
    CHECK(!cv_pal_at(0)->blend);
    char u[CV_PAL_NAME];
    cv_pal_unique("mine", u, sizeof u);
    CHECK(!strcmp(u, "mine 2"));
    cv_pal_unique("new", u, sizeof u);
    CHECK(!strcmp(u, "new"));
    CHECK(!cv_pal_rename(1, "mine"));
    CHECK(!cv_pal_rename(1, "  "));
    CHECK(cv_pal_rename(1, "other"));
    cv_pal_move(1, 0);
    CHECK(!strcmp(cv_pal_at(0)->name, "other") && !strcmp(cv_pal_at(1)->name, "mine"));
    cv_pal_remove(0);
    CHECK_EQ(cv_pal_count(), 1);
    CHECK(!strcmp(cv_pal_at(0)->name, "mine"));
    CHECK(cv_pal_at(1) == NULL && cv_pal_at(-1) == NULL);
    p.n = 1;
    CHECK_EQ(cv_pal_add(&p), -1);

    /* import: the issue's JSON (a fragment, without braces around it) and XML */
    cv_pal_clear();
    int bad = -1;
    const char* js = "\"UserDefinedPalettes\": {\n  \"default\": [\n    \"211,211,211,255,LightGray\",\n"
                     "    \"0,92,255,255,0_92_255_255\",\n    \"...\",\n    \"255,0,0,255,Red\"\n  ],\n"
                     "  \"two\": [[0,0,0], [255,255,255,255]], \"skip\": 3\n}";
    CHECK_EQ(cv_pal_import(js, &bad), 2);
    CHECK_EQ(bad, 1);                                    /* the "..." */
    CHECK_EQ(cv_pal_count(), 2);
    const cv_pal* d = cv_pal_at(cv_pal_find("default"));
    CHECK(d && d->n == 3 && d->rgb[1][1] == 92 && d->rgb[2][0] == 255 && !d->blend);
    d = cv_pal_at(cv_pal_find("two"));
    CHECK(d && d->n == 2 && d->rgb[1][2] == 255);

    const char* xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<CCXVIEW version=\"3.0\">\n"
                      " <head title=\"CCXVIEW Palettes\"/>\n <body>\n  <Settings>\n   <UserDefinedColors/>\n"
                      "   <UserDefinedPalettes>\n    <default value=\"211,211,211,255,LightGray\"/>\n"
                      "    <default value=\"0,92,255,255,0_92_255_255\"/>\n    <default value=\"255,185,0,255,255_185_0_255\"/>\n"
                      "    <default value=\"255,0,0,255,Red\"/>\n    <cold value='0,0,255'/>\n    <cold value=\"#00ffff\"/>\n"
                      "   </UserDefinedPalettes>\n  </Settings>\n </body>\n</CCXVIEW>\n";
    CHECK_EQ(cv_pal_import(xml, &bad), 2);
    CHECK_EQ(bad, 0);
    CHECK_EQ(cv_pal_count(), 3);                         /* default replaced, cold added */
    d = cv_pal_at(cv_pal_find("default"));
    CHECK(d && d->n == 4 && d->rgb[2][1] == 185);
    d = cv_pal_at(cv_pal_find("cold"));
    CHECK(d && d->n == 2 && d->rgb[1][1] == 255);
    CHECK_EQ(cv_pal_import("<UserDefinedPalettes/>", &bad), 0);
    CHECK_EQ(cv_pal_import("nothing here", &bad), 0);
    CHECK_EQ(cv_pal_import("{ \"x\": [\"1,2,3\"", &bad), 0);   /* cut short: nothing half read */

    /* export and back, with the blend flag and a name that needs escaping */
    cv_pal_at(cv_pal_find("cold"))->blend = true;
    cv_pal_rename(cv_pal_find("two"), "say \"hi\"");
    char* j = cv_pal_export_json(-1);
    CHECK(j && strstr(j, "\"UserDefinedPalettes\"") && strstr(j, "\"211,211,211,255\"") && strstr(j, "\"Blended\": [\"cold\"]"));
    cv_pal_clear();
    CHECK_EQ(cv_pal_import(j, &bad), 3);
    CHECK_EQ(bad, 0);
    CHECK(cv_pal_find("say \"hi\"") >= 0);
    d = cv_pal_at(cv_pal_find("cold"));
    CHECK(d && d->blend && d->n == 2);
    CHECK(!cv_pal_at(cv_pal_find("default"))->blend);
    free(j);
    j = cv_pal_export_json(cv_pal_find("cold"));
    CHECK(j && strstr(j, "cold") && !strstr(j, "default"));
    free(j);
    cv_pal_clear();
}
