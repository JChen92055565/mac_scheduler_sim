#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#define DEBUG_QUEUES 0     // set to 1 if you want verbose per-TTI prints
#define MAX_QUEUE 4096

// ----------------- Packets -----------------
typedef struct {
    int bits;           // remaining payload (bits)
    int arrival_tti;    // when it arrived
    int deadline_tti;   // absolute deadline TTI
} Packet;

// ----------------- UE -----------------
typedef struct {
    int id;
    int cqi;                    // 1..15 (legacy or PHY-derived)
    Packet *q;                  // circular buffer
    int q_head, q_tail, q_count;
    long long bits_sent_total;
    long long pkts_delivered;
    long long pkts_missed;

    // PHY snapshot (set each TTI when phy_mode==1)
    int    bprb_cur;           // bits per RB for this TTI from PHY
    double sinr_db_cur;        // instantaneous SINR in dB
    double rb_err_prob_cur;    // per-RB error probability at TX time

    // debug (per TTI)
    int dbg_tx_bits_this_tti;
    int dbg_was_scheduled;
} UE;

// ----------------- Config -----------------
typedef struct {
    int ttis;               // total simulation length
    int rb_total;           // total RBs per TTI (capacity / energy proxy)
    int num_ues;            // number of UEs
    unsigned int seed;      // rng seed
    double arrival_rate;    // probability of one packet arrival per UE per TTI (Bernoulli)
    int pkt_bits_min;       // min packet size (bits)
    int pkt_bits_max;       // max packet size (bits)
    int deadline_ttis;      // relative deadline (TTIs after arrival)
    double bler;            // base BLER for HARQ success (0.0..1.0) [legacy mode]
    int harq_rtt;           // HARQ round-trip in TTIs
    const char *out_dir;    // (unused for now)
    const char *csv_path;

    // -------- PHY / channel model params --------
    int    phy_mode;          // 0 = legacy (random-walk CQI + fixed BLER), 1 = channel-based
    double pathloss_exp;      // e.g., 3.5
    double shadowing_std_db;  // e.g., 6.0
    double fading_rho;        // AR(1) coefficient (0..1)
    double snr_ref_db;        // reference SNR (median) in dB
    double rb_floor_perr;     // minimum RB error probability (e.g., 1e-4)
} Config;

// ----------------- Metrics -----------------
typedef struct {
    long long total_bits_sent;
    long long total_packets;
    long long deadline_misses;
    double    avg_latency;          // not directly used
    long long sum_latency;          // accumulate latency (TTIs) of delivered pkts

    long long rb_used_total;        // for utilization
} Metrics;

// ----------------- HARQ feedback event -----------------
typedef struct {
    int ue_id;
    int tti_feedback;      // when ACK/NACK arrives (now + harq_rtt)
    int pkt_arrival_tti;   // for latency calc
    int pkt_deadline_tti;  // for potential miss logic
    int pkt_size_bits;     // size of the TB we just sent
    int retx_count;        // retransmissions so far

    // Context captured at transmit time (for PHY-based feedback)
    int    rb_alloc;              // RBs allocated for this TB
    int    cqi_at_tx;             // CQI at TX time
    double sinr_db_at_tx;         // SINR at TX time
    double rb_err_prob_at_tx;     // per-RB error probability used for this TX
} HarqEvent;

// ----------------- Simple RNG helpers -----------------
static inline void rng_seed(unsigned int s) { srand(s); }
static inline double rng_uniform01(void) { return (rand() + 1.0) / (RAND_MAX + 2.0); }
static inline int rng_int(int lo, int hi) { // inclusive bounds
    if (hi <= lo) return lo;
    return lo + (int)floor(rng_uniform01() * (double)(hi - lo + 1));
}

#endif // COMMON_H
