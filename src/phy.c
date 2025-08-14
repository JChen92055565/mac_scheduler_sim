#include "phy.h"
#define M_PI 3.14159265358979323846

// Clamp utility
static inline double clamp(double x, double lo, double hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

// Simple random normal via Box-Muller (for shadowing / fading innovation)
static double rng_norm() {
    double u1 = rng_uniform01();
    double u2 = rng_uniform01();
    double r = sqrt(-2.0 * log(u1 + 1e-12));
    double th = 2.0 * M_PI * u2;
    return r * cos(th);
}

// Very simple placement: distance in [0.5, 1.5] cell radii
static double draw_distance() {
    // Area-uniform in annulus [r1, r2]
    double r1 = 0.5, r2 = 1.5;
    double u  = rng_uniform01();
    return sqrt(u*(r2*r2 - r1*r1) + r1*r1);
}

// CQI mapping from SINR (dB): coarse thresholds (illustrative)
int phy_map_sinr_to_cqi(double sinr_db) {
    // thresholds roughly spanning -5..25 dB to CQI 1..15
    static const double th[15] = {
        -5, -2,  0,  1.5, 3,  5,  7,   9,
        11, 13, 15, 17,  19, 21, 23
    };
    int cqi = 1;
    for (int i = 0; i < 15; ++i) {
        if (sinr_db >= th[i]) cqi = i + 1;
    }
    if (cqi < 1) cqi = 1;
    if (cqi > 15) cqi = 15;
    return cqi;
}

int phy_bits_per_rb_for_cqi(int cqi) {
    static const int table[16] = {
        0,   48,   72,   96,  120,  144,  192,  240,
      288,  336,  408,  480,  552,  648,  744,  840
    };
    if (cqi < 1) cqi = 1;
    if (cqi > 15) cqi = 15;
    return table[cqi];
}

void phy_init(Phy *p, const Config *cfg, int num_ues, unsigned int seed) {
    (void)seed; // rng_seed already set by caller
    p->num_ues = num_ues;
    p->ue = (PhyUEState*)calloc(num_ues, sizeof(PhyUEState));
    for (int i = 0; i < num_ues; ++i) {
        double d = draw_distance(); // arbitrary units
        double pl = 10.0 * cfg->pathloss_exp * log10(d);
        double sh = cfg->shadowing_std_db * rng_norm(); // log-normal in dB (approx)
        p->ue[i].pathloss_db = pl;
        p->ue[i].shadow_db   = sh;
        p->ue[i].fading_state = 0.0; // start at 0
    }
}

void phy_free(Phy *p) {
    if (!p) return;
    free(p->ue);
    p->ue = NULL;
    p->num_ues = 0;
}

void phy_step(Phy *p, const Config *cfg, int now_tti) {
    (void)now_tti;
    double rho = clamp(cfg->fading_rho, 0.0, 0.999);
    double sigma = sqrt(fmax(1e-9, 1.0 - rho*rho)); // innovation std
    for (int i = 0; i < p->num_ues; ++i) {
        double z = rng_norm();
        p->ue[i].fading_state = rho * p->ue[i].fading_state + sigma * z;
    }
}

static double per_rb_from_sinr(double sinr_db, double floor_perr) {
    // Simple logistic PER curve centered near ~8 dB with slope ~0.8
    double snr50 = 8.0;
    double k = 0.8;
    double p = 1.0 / (1.0 + exp(k * (sinr_db - snr50)));
    if (p < floor_perr) p = floor_perr;
    if (p > 1.0) p = 1.0;
    return p;
}

void phy_get_instant(const Phy *p, const Config *cfg, int ue_id, PhyUEInstant *out) {
    const PhyUEState *st = &p->ue[ue_id];

    // Convert fading_state (~N(0,1)) to dB ripple ~ ± a few dB
    double fading_db = 3.0 * st->fading_state; // scale for visibility
    double sinr_db = cfg->snr_ref_db - st->pathloss_db - st->shadow_db + fading_db;
    sinr_db = clamp(sinr_db, -10.0, 30.0);

    int cqi = phy_map_sinr_to_cqi(sinr_db);
    int bprb = phy_bits_per_rb_for_cqi(cqi);

    double perrb = per_rb_from_sinr(sinr_db, cfg->rb_floor_perr);

    out->sinr_db    = sinr_db;
    out->cqi        = cqi;
    out->bits_per_rb= bprb;
    out->rb_err_prob= perrb;
}

void phy_on_retx(Phy *p, int ue_id, int retx_count) {
    (void)p; (void)ue_id; (void)retx_count;
}
