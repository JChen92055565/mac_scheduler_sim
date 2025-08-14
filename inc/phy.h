#ifndef PHY_H
#define PHY_H

#include "common.h"

// Per-UE persistent channel state (large-scale + fading state)
typedef struct {
    double pathloss_db;
    double shadow_db;
    double fading_state;   // AR(1) state (linear, not dB)
} PhyUEState;

// Per-UE instantaneous snapshot (computed each TTI)
typedef struct {
    double sinr_db;
    int    cqi;
    int    bits_per_rb;
    double rb_err_prob;
} PhyUEInstant;

typedef struct {
    PhyUEState *ue;  // array size = num_ues
    int num_ues;
    // optional globals
} Phy;

// API
void  phy_init(Phy *p, const Config *cfg, int num_ues, unsigned int seed);
void  phy_free(Phy *p);
void  phy_step(Phy *p, const Config *cfg, int now_tti);
void  phy_get_instant(const Phy *p, const Config *cfg, int ue_id, PhyUEInstant *out);
void  phy_on_retx(Phy *p, int ue_id, int retx_count); // optional no-op for now

// Helpers exposed so scheduler/legacy can reuse table if desired
int   phy_map_sinr_to_cqi(double sinr_db);
int   phy_bits_per_rb_for_cqi(int cqi);

#endif // PHY_H
