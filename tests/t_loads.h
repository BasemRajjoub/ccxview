/* t_loads.h -- the loads and supports of a deck, per step (inp.c, inp_loads.c) */

static const cv_cload* t_cload(const cv_applied* a, uint32_t node, int dof) {
    for (uint32_t i = 0; i < a->ncloads; i++) if (a->cloads[i].node == node && a->cloads[i].dof == dof) return &a->cloads[i];
    return NULL;
}
static const cv_bc* t_bc(const cv_applied* a, uint32_t node, int dof) {
    for (uint32_t i = 0; i < a->nbcs; i++) if (a->bcs[i].node == node && a->bcs[i].dof_lo == dof) return &a->bcs[i];
    return NULL;
}
static int t_dloads(const cv_applied* a, int kind) {
    int n = 0;
    for (uint32_t i = 0; i < a->ndloads; i++) n += a->dloads[i].kind == kind;
    return n;
}
static const cv_body* t_body(const cv_applied* a, int kind) {
    for (uint32_t i = 0; i < a->nbody; i++) if (a->body[i].kind == kind) return &a->body[i];
    return NULL;
}

static void test_loads(void) {
    const char* deck =
        "*NODE, NSET=NALL\n"
        "1, 0,0,0\n2, 1,0,0\n3, 1,1,0\n4, 0,1,0\n5, 0,0,1\n6, 1,0,1\n7, 1,1,1\n8, 0,1,1\n9, 5,5,5\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EALL\n1, 1,2,3,4,5,6,7,8\n"
        "*ELEMENT, TYPE=S4, ELSET=ESH\n2, 1,2,3,4\n"
        "*SURFACE, NAME=STOP\nEALL, S2\n"
        "*SURFACE, NAME=SCUT\n1, S1\n"
        "*PRE-TENSION SECTION, SURFACE=SCUT, NODE=9\n0., 0., 1.\n"
        "*MPC\nBEAM, 5, 6\nPLANE, 1, 2, 3,\n4\n"
        "*CYCLIC SYMMETRY MODEL, N=12, TIE=T\n0,0,0, 0,0,1\n"
        "*BOUNDARY\n1, 1, 3\n"                       /* before any step: stays whatever the steps do */
        "*STEP\n*STATIC\n"
        "*BOUNDARY\n2, 2, 2, 0.5\n"                  /* a prescribed displacement */
        "*CLOAD\n7, 3, 10.\n7, 5, 4.\n9, 1, 1000.\n"
        "*DLOAD\n1, P3, 2.\nEALL, GRAV, 9.81, 0., 0., -1.\nEALL, CENTRIF, 100., 0,0,0, 0,0,1\nEALL, BY, 3.\n2, EDNOR2, 7.\n"
        "*DSLOAD\nSTOP, P, 5.\n"
        "*END STEP\n"
        "*STEP\n*STATIC\n"
        "*CLOAD\n7, 3, 20.\n"                        /* the force changes, the moment stays */
        "*DLOAD, OP=NEW\n1, P4, 1.\n"                /* all pressures and body loads go, one comes */
        "*END STEP\n"
        "*STEP\n*HEAT TRANSFER\n"
        "*BOUNDARY, OP=NEW\n3, 11, 11, 300.\n"
        "*CLOAD, OP=NEW\n"
        "*CFLUX\n5, 11, 2.5\n"
        "*DFLUX\n1, S2, 8.\nEALL, BF, 0.1\n"
        "*FILM\n1, F3, 293., 25.\n"
        "*RADIATE\n1, R4, 293., 0.8\n"
        "*TEMPERATURE\nNALL, 400.\n"
        "*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK_EQ(d.nsteps, 3);
    CHECK_EQ(d.npret, 1);
    if (d.npret == 1) { CHECK(d.pret[0].surf >= 0); CHECK_EQ(d.pret[0].ref, 9); CHECK(d.pret[0].has_dir); CHECK_NEAR(d.pret[0].dir[2], 1, 0); }
    CHECK_EQ(d.cyc_n, 12);
    CHECK_NEAR(d.cyc_axis[5], 1, 0);
    int mpc = 0;
    for (int i = 0; i < d.nlinks; i++)
        if (!strncmp(d.links[i].name, "MPC ", 4)) { mpc++; CHECK_EQ(d.links[i].n, mpc == 1 ? 2 : 4); }
    CHECK_EQ(mpc, 2);

    cv_applied a;
    CHECK(cv_inp_applied(&d, 0, &a));
    CHECK(t_bc(&a, 1, 1) && t_bc(&a, 1, 3) && !t_bc(&a, 1, 4));
    CHECK(t_bc(&a, 2, 2) && t_bc(&a, 2, 2)->value == 0.5f);
    CHECK(t_cload(&a, 7, 3) && t_cload(&a, 7, 3)->value == 10.f);
    CHECK(t_cload(&a, 7, 5) != NULL);
    CHECK_EQ(t_dloads(&a, CV_DL_P), 2);              /* P3 and the surface's S2 */
    CHECK_EQ(t_dloads(&a, CV_DL_EDGE), 1);
    CHECK(t_body(&a, CV_BL_GRAV) && t_body(&a, CV_BL_GRAV)->v[2] == -1.f && t_body(&a, CV_BL_GRAV)->set >= 0);
    CHECK(t_body(&a, CV_BL_CENTRIF) && t_body(&a, CV_BL_CENTRIF)->v[5] == 1.f);
    CHECK(t_body(&a, CV_BL_FORCE) && t_body(&a, CV_BL_FORCE)->v[1] == 1.f);
    CHECK_EQ(a.ntemps, 0);
    cv_applied_free(&a);

    CHECK(cv_inp_applied(&d, 1, &a));
    CHECK(t_cload(&a, 7, 3) && t_cload(&a, 7, 3)->value == 20.f);
    CHECK(t_cload(&a, 7, 5) && t_cload(&a, 7, 5)->value == 4.f);
    CHECK_EQ(a.ndloads, 1);
    CHECK_EQ(a.nbody, 0);
    CHECK(t_bc(&a, 2, 2) != NULL);
    cv_applied_free(&a);

    CHECK(cv_inp_applied(&d, 2, &a));
    CHECK(t_bc(&a, 1, 1) != NULL);                   /* model data survives OP=NEW */
    CHECK(!t_bc(&a, 2, 2));
    CHECK(t_bc(&a, 3, 11) && t_bc(&a, 3, 11)->value == 300.f);
    CHECK(!t_cload(&a, 7, 3));
    CHECK(t_cload(&a, 5, 11) && t_cload(&a, 5, 11)->value == 2.5f);
    CHECK_EQ(t_dloads(&a, CV_DL_P), 1);              /* thermal cards leave the pressure */
    CHECK_EQ(t_dloads(&a, CV_DL_FLUX), 1);
    CHECK_EQ(t_dloads(&a, CV_DL_FILM), 1);
    CHECK_EQ(t_dloads(&a, CV_DL_RAD), 1);
    for (uint32_t i = 0; i < a.ndloads; i++) {
        if (a.dloads[i].kind == CV_DL_FILM) { CHECK_EQ(a.dloads[i].face, 2); CHECK_NEAR(a.dloads[i].value, 25, 0); }
        if (a.dloads[i].kind == CV_DL_RAD) CHECK_NEAR(a.dloads[i].value, 0.8, 1e-6);
    }
    CHECK(t_body(&a, CV_BL_HEAT) != NULL);
    CHECK_EQ(a.ntemps, 9);
    cv_applied_free(&a);

    cv_applied last, out;                            /* a step that does not exist: the last one */
    CHECK(cv_inp_applied(&d, 2, &last) && cv_inp_applied(&d, 99, &out));
    CHECK_EQ(last.nbcs, out.nbcs); CHECK_EQ(last.ncloads, out.ncloads); CHECK_EQ(last.ndloads, out.ndloads);
    cv_applied_free(&last); cv_applied_free(&out);
    cv_inp_free(&d); free(d.msgs.a);

    /* the catalogue deck (samples/symbols): every card of it is understood */
    FILE* fp = fopen("samples/symbols/symbols.inp", "rb");
    CHECK(fp != NULL);
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char* buf = malloc((size_t)n + 1);
        CHECK(buf && fread(buf, 1, (size_t)n, fp) == (size_t)n);
        fclose(fp);
        cv_inp c;
        CHECK(cv_inp_parse(&c, buf, (size_t)n, NULL, NULL));
        for (size_t i = 0; i < c.msgs.n; i++) CHECK(!strstr(c.msgs.a[i].text, "could not be read"));
        CHECK_EQ(c.nsteps, 3);
        CHECK_EQ(c.npret, 3);                        /* two bolts and one buried in a block */
        CHECK_EQ(c.ntransforms, 1);
        cv_applied h, m, l;                          /* heat step, all loads, the last step */
        CHECK(cv_inp_applied(&c, 0, &h) && cv_inp_applied(&c, 1, &m) && cv_inp_applied(&c, 2, &l));
        CHECK(t_dloads(&h, CV_DL_FLUX) == 2 && t_dloads(&h, CV_DL_FILM) == 2 && t_dloads(&h, CV_DL_RAD) == 1);
        CHECK(t_body(&h, CV_BL_HEAT) && !t_body(&h, CV_BL_GRAV) && h.ntemps == 0);
        int forces = 0, moments = 0, heat = 0;
        for (uint32_t i = 0; i < h.ncloads; i++) { forces += h.cloads[i].dof <= 3; heat += h.cloads[i].dof == 11; }
        CHECK(forces == 0 && heat == 2);
        forces = 0;
        for (uint32_t i = 0; i < m.ncloads; i++) { forces += m.cloads[i].dof <= 3; moments += m.cloads[i].dof >= 4 && m.cloads[i].dof <= 6; }
        CHECK(forces > 10 && moments == 3);          /* two on beams, one on a rigid body's ROT NODE */
        {   /* the last step holds the first bolt's preload (*BOUNDARY, FIXED on its reference node) */
            bool held = false;
            for (uint32_t i = 0; i < l.nbcs; i++) held |= l.bcs[i].node == c.pret[0].ref;
            CHECK(held);
        }
        CHECK(t_dloads(&m, CV_DL_EDGE) == 1 && t_dloads(&m, CV_DL_P) >= 5);
        CHECK(t_body(&m, CV_BL_GRAV) && t_body(&m, CV_BL_CENTRIF) && m.ntemps == 4);
        bool prescribed = false;
        for (uint32_t i = 0; i < m.nbcs; i++) prescribed |= m.bcs[i].dof_lo == 3 && m.bcs[i].value == 0.02f;
        CHECK(prescribed);
        forces = 0; heat = 0;                        /* *CLOAD, OP=NEW: one force left, the heat stays */
        for (uint32_t i = 0; i < l.ncloads; i++) { forces += l.cloads[i].dof <= 6; heat += l.cloads[i].dof == 11; }
        CHECK(forces == 1 && heat == 2);
        CHECK(t_dloads(&l, CV_DL_P) == t_dloads(&m, CV_DL_P) + 1);
        cv_applied_free(&h); cv_applied_free(&m); cv_applied_free(&l);
        cv_inp_free(&c); free(c.msgs.a);
        free(buf);
    }
}

/* amplitudes, step times, *MODEL CHANGE and *SUBMODEL (inp.c, inp_steps.c) */
static void test_steps(void) {
    const char* deck =
        "*NODE, NSET=NALL\n"
        "1, 0,0,0\n2, 1,0,0\n3, 1,1,0\n4, 0,1,0\n5, 0,0,1\n6, 1,0,1\n7, 1,1,1\n8, 0,1,1\n"
        "9, 2,0,0\n10, 2,1,0\n11, 2,0,1\n12, 2,1,1\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EA\n1, 1,2,3,4,5,6,7,8\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EB\n2, 2,9,10,3,6,11,12,7\n"
        "*NSET, NSET=NCUT\n1, 4, 5, 8\n"
        "*SURFACE, NAME=SB\nEB, S4\n"
        "*SURFACE, NAME=SM\nEA, S3\n"
        "*SURFACE, NAME=SS\nEB, S3\n"
        "*SURFACE INTERACTION, NAME=I0\n*SURFACE BEHAVIOR, PRESSURE-OVERCLOSURE=HARD\n"
        "*SURFACE INTERACTION, NAME=i1\n*SURFACE BEHAVIOR, PRESSURE-OVERCLOSURE=LINEAR\n1e6\n*FRICTION\n0.3, 5e4\n"
        "*CONTACT PAIR, INTERACTION=I1, TYPE=SURFACE TO SURFACE\nSS, SM\n"
        "*AMPLITUDE, NAME=RAMP\n0., 0., 1., 1., 2., 0.5\n"
        "*AMPLITUDE, NAME=TOT, TIME=TOTAL TIME, SHIFTY=1.\n0., 0.\n4., 4.\n"
        "*AMPLITUDE, NAME=SM1, DEFINITION=SMOOTH STEP\n0., 0., 1., 1.\n"
        "*SUBMODEL, TYPE=NODE, INPUT=global.frd\nNCUT, 12\n"
        "*SUBMODEL, TYPE=SURFACE, INPUT=global.frd, GLOBAL ELSET=EG\nSB\n"
        "*STEP\n*STATIC\n0.1, 2.\n"
        "*BOUNDARY, SUBMODEL, STEP=2\nNCUT, 1, 3\n"
        "*CLOAD, AMPLITUDE=RAMP\n7, 3, 10.\n"
        "*CLOAD, AMPLITUDE=TOT\n6, 1, 2.\n"
        "*DLOAD, AMPLITUDE=SM1\n1, P2, 3.\n"
        "*DSLOAD, SUBMODEL, STEP=1\nSB, P\n"
        "*TEMPERATURE, AMPLITUDE=RAMP\n7, 100.\n"
        "*END STEP\n"
        "*STEP\n*FREQUENCY\n4\n*END STEP\n"
        "*STEP\n*STATIC\n"
        "*MODEL CHANGE, TYPE=ELEMENT, REMOVE\nEB\n"
        "*MODEL CHANGE, TYPE=CONTACT PAIR, REMOVE\nSS, SM\n"
        "*END STEP\n"
        "*STEP\n*HEAT TRANSFER\n0.5, 3.\n"
        "*MODEL CHANGE, TYPE=ELEMENT, ADD=STRAIN FREE\n2\n"
        "*MODEL CHANGE, TYPE=ELEMENT, REMOVE\n1\n"
        "*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK_EQ(d.nsteps, 4);
    CHECK_EQ(d.namps, 3);
    if (d.namps == 3) {
        CHECK(d.amps[0].tabular && !d.amps[0].total && d.amps[0].n == 3);
        CHECK_NEAR(cv_amp_at(&d.amps[0], 0.5), 0.5, 1e-6);
        CHECK_NEAR(cv_amp_at(&d.amps[0], 1.5), 0.75, 1e-6);
        CHECK_NEAR(cv_amp_at(&d.amps[0], 9.), 0.5, 1e-6);    /* constant beyond the ends */
        CHECK_NEAR(cv_amp_at(&d.amps[0], -1.), 0., 1e-6);
        CHECK(d.amps[1].total && d.amps[1].n == 2);
        CHECK_NEAR(cv_amp_at(&d.amps[1], 2.), 3., 1e-6);     /* shifted up by 1 */
        CHECK(!d.amps[2].tabular && !strcmp(d.amps[2].def, "SMOOTH STEP"));
        CHECK(isnan(cv_amp_at(&d.amps[2], 0.5)));
    }
    if (d.stepinfo && d.nsteps == 4) {
        CHECK(!strcmp(d.stepinfo[0].proc, "STATIC") && d.stepinfo[0].period == 2.f);
        CHECK(!strcmp(d.stepinfo[1].proc, "FREQUENCY") && d.stepinfo[1].period == 0.f);
        CHECK(d.stepinfo[2].period == 1.f && !strcmp(d.stepinfo[3].proc, "HEATTRANSFER") && d.stepinfo[3].period == 3.f);
        CHECK_NEAR(cv_inp_step_start(&d, 3), 3., 1e-9);
    }
    /* the values at step time 1 of step 1 (total time 1) */
    cv_applied a;
    CHECK(cv_inp_applied(&d, 0, &a));
    CHECK(t_cload(&a, 7, 3) && t_cload(&a, 7, 3)->amp == 1);
    cv_applied_at(&d, &a, 1., 1.);
    CHECK(t_cload(&a, 7, 3) && t_cload(&a, 7, 3)->value == 10.f);
    CHECK(t_cload(&a, 6, 1) && t_cload(&a, 6, 1)->value == 4.f);                  /* 2 x (1 + 1) */
    CHECK(a.ntemps == 1 && a.temps[0].value == 100.f);
    int sub = 0, subp = 0, p = 0;
    for (uint32_t i = 0; i < a.nbcs; i++) sub += a.bcs[i].sub == 2;
    for (uint32_t i = 0; i < a.ndloads; i++) { subp += a.dloads[i].sub == 1; p += !a.dloads[i].sub && a.dloads[i].value == 3.f; }
    CHECK_EQ(sub, 12);                                /* 4 nodes x 3 DOFs */
    CHECK_EQ(subp, 1);
    CHECK_EQ(p, 1);                                   /* SMOOTH STEP is not evaluated: the line's value */
    cv_applied_free(&a);
    CHECK(cv_inp_applied(&d, 0, &a));
    cv_applied_at(&d, &a, 0.5, 0.5);
    CHECK(t_cload(&a, 7, 3) && t_cload(&a, 7, 3)->value == 5.f);
    cv_applied_free(&a);

    CHECK_EQ(d.nsubs, 2);
    if (d.nsubs == 2) {
        CHECK(!d.subs[0].surface && d.subs[0].nn == 5 && !strcmp(d.subs[0].input, "global.frd"));
        CHECK(d.subs[1].surface && d.subs[1].nsurf == 1 && !strcmp(d.subs[1].gelset, "EG"));
    }
    /* *MODEL CHANGE: EB out from step 3, back in step 4 when element 1 goes */
    uint32_t* ids = NULL;
    CHECK_EQ(cv_inp_removed(&d, 0, &ids), 0);
    CHECK_EQ(cv_inp_removed(&d, 1, &ids), 0);
    CHECK_EQ(cv_inp_removed(&d, 2, &ids), 1);
    CHECK(ids && ids[0] == 2);
    free(ids);
    CHECK_EQ(cv_inp_removed(&d, 3, &ids), 1);
    CHECK(ids && ids[0] == 1);
    free(ids);
    int pair = -1;
    for (int k = 0; k < d.nlinks; k++) if (d.links[k].kind == CV_LINK_CONTACT) pair = k;
    CHECK(pair >= 0 && cv_inp_link_active(&d, 1, pair) && !cv_inp_link_active(&d, 2, pair));
    if (pair >= 0) CHECK_NEAR(d.links[pair].mu, 0.3, 1e-6);          /* *FRICTION of its interaction, any case */
    cv_inp_free(&d); free(d.msgs.a);
}

/* *CLOAD and *TEMPERATURE with SUBMODEL, TIME DELAY=, LOAD CASE= (inp.c) */
static bool t_unreadable(const cv_inp* d) {
    for (size_t i = 0; i < d->msgs.n; i++) if (strstr(d->msgs.a[i].text, "unreadable")) return true;
    return false;
}
static void test_load_cards(void) {
    const char* deck =
        "*NODE, NSET=NALL\n"
        "1, 0,0,0\n2, 1,0,0\n3, 1,1,0\n4, 0,1,0\n5, 0,0,1\n6, 1,0,1\n7, 1,1,1\n8, 0,1,1\n"
        "*ELEMENT, TYPE=C3D8, ELSET=EA\n1, 1,2,3,4,5,6,7,8\n"
        "*NSET, NSET=NCUT\n1, 4, 5, 8\n"
        "*AMPLITUDE, NAME=RAMP\n0., 0., 1., 1.\n"
        "*SUBMODEL, TYPE=NODE, INPUT=global.frd\nNCUT\n"
        "*STEP\n*STATIC\n"
        "*CLOAD, SUBMODEL, STEP=2\nNCUT, 1\nNCUT, 2\n8, 3\n"
        "*TEMPERATURE, SUBMODEL, STEP=3\nNCUT\n"
        "*CLOAD, AMPLITUDE=RAMP, TIME DELAY=0.5\n7, 3, 10.\n"
        "*CLOAD, AMPLITUDE=RAMP, TIME DELAY=0.5\n6, 3, 10.\n"
        "*CLOAD, AMPLITUDE=RAMP\n2, 3, 10.\n"
        "*END STEP\n"
        "*STEP\n*STEADY STATE DYNAMICS\n1., 10., 5\n"
        "*CLOAD, LOAD CASE=1\n3, 1, 7.\n"
        "*CLOAD, LOAD CASE=2, OP=NEW\n3, 1, 9.\n"
        "*END STEP\n";
    cv_inp d;
    CHECK(cv_inp_parse(&d, deck, strlen(deck), NULL, NULL));
    CHECK(!t_unreadable(&d));                             /* the SUBMODEL lines are read, not skipped */
    CHECK_EQ(d.namps, 2);                                 /* RAMP, and RAMP later by 0.5 (made once) */
    if (d.namps == 2) {
        CHECK(!strcmp(d.amps[1].name, "RAMP DELAY 0.5"));
        CHECK_NEAR(cv_amp_at(&d.amps[1], 0.5), 0., 1e-6);
        CHECK_NEAR(cv_amp_at(&d.amps[1], 1.0), 0.5, 1e-6);
    }
    cv_applied a;
    CHECK(cv_inp_applied(&d, 0, &a));
    int f = 0, t = 0;
    for (uint32_t i = 0; i < a.ncloads; i++) f += a.cloads[i].sub == 2;
    for (uint32_t i = 0; i < a.ntemps; i++) t += a.temps[i].sub == 3;
    CHECK_EQ(f, 9);                                       /* 4 nodes x 2 DOFs, node 8 a third */
    CHECK_EQ(t, 4);
    CHECK(t_cload(&a, 7, 3) && t_cload(&a, 7, 3)->amp == 2 && t_cload(&a, 6, 3) && t_cload(&a, 6, 3)->amp == 2);
    cv_applied_at(&d, &a, 1., 1.);                        /* at step time 1: delayed by 0.5, half way up */
    CHECK(t_cload(&a, 7, 3) && t_cload(&a, 7, 3)->value == 5.f);
    CHECK(t_cload(&a, 2, 3) && t_cload(&a, 2, 3)->value == 10.f);
    cv_applied_free(&a);
    CHECK(cv_inp_applied(&d, 1, &a));                     /* load case 2 left out, its OP=NEW too */
    CHECK(t_cload(&a, 3, 1) && t_cload(&a, 3, 1)->value == 7.f);
    CHECK(t_cload(&a, 2, 3) != NULL);
    cv_applied_free(&a);
    cv_inp_free(&d); free(d.msgs.a);
}
