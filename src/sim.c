#include "sim.h"
#include "scheduler.h"
#include "metrics.h"

// ----------------- UE queue helpers -----------------

static void ue_queue_init(UE *u) {
    u->q = (Packet*)calloc(MAX_QUEUE, sizeof(Packet));
    u->q_head = u->q_tail = u->q_count = 0;
    u->bits_sent_total = 0;
    u->pkts_delivered = 0;
    u->pkts_missed = 0;
    u->cqi = rng_int(6, 12); // legacy init
    u->bprb_cur = 0;
    u->sinr_db_cur = 0.0;
    u->rb_err_prob_cur = 0.0;
    u->dbg_tx_bits_this_tti = 0;
    u->dbg_was_scheduled = 0;
}

static bool ue_queue_push(UE *u, Packet p) {
    if (u->q_count >= MAX_QUEUE) return false;
    u->q[u->q_tail] = p;
    u->q_tail = (u->q_tail + 1) % MAX_QUEUE;
    u->q_count++;
    return true;
}

static bool ue_queue_peek(UE *u, Packet *out) {
    if (u->q_count == 0) return false;
    *out = u->q[u->q_head];
    return true;
}

static void ue_queue_pop(UE *u) {
    if (u->q_count == 0) return;
    u->q_head = (u->q_head + 1) % MAX_QUEUE;
    u->q_count--;
}

// ----------------- Sim lifecycle -----------------

void sim_init(Sim *s, const Config *cfg) {
    s->cfg = *cfg;
    s->tti = 0;
    s->m = (Metrics){0};
    rng_seed(s->cfg.seed);

    s->ues = (UE*)calloc(cfg->num_ues, sizeof(UE));
    for (int i = 0; i < cfg->num_ues; ++i) {
        s->ues[i].id = i;
        ue_queue_init(&s->ues[i]);
    }
    // HARQ ring buffer
    int cap = s->cfg.ttis * s->cfg.num_ues + 1024;
    s->harq_events = (HarqEvent*)calloc(cap, sizeof(HarqEvent));
    s->harq_cap = cap;
    s->harq_head = s->harq_tail = s->harq_count = 0;

    // CSV logging
    s->csv = NULL;
    if (cfg->csv_path && *cfg->csv_path) {
        s->csv = fopen(cfg->csv_path, "w");
        if (s->csv) {
            // header
            fprintf(s->csv, "tti,ue,bits_sent,rb_used,cqi,queue_after,hol_deadline\n");
            fflush(s->csv);
        }
    }
    s->evcsv = fopen("data/events.csv", "w");
    if (s->evcsv) {
        fprintf(s->evcsv, "tti,event,ue,pkt_bits,retx,sinr_db,cqi,rb_alloc,rb_perr\n");
        fflush(s->evcsv);
    }
    s->chcsv = fopen("data/channel.csv", "w");
    if (s->chcsv) {
        fprintf(s->chcsv, "tti,ue,sinr_db,cqi,bits_per_rb,rb_err_prob\n");
        fflush(s->chcsv);
    }

    // PHY
    if (s->cfg.phy_mode == 1) {
        phy_init(&s->phy, &s->cfg, s->cfg.num_ues, s->cfg.seed ^ 0xC0FFEEu);
    } else {
        memset(&s->phy, 0, sizeof(s->phy));
    }
}

void sim_free(Sim *s) {
    if (!s) return;
    if (s->ues) {
        for (int i = 0; i < s->cfg.num_ues; ++i) free(s->ues[i].q);
        free(s->ues);
    }
    if (s->harq_events) free(s->harq_events);
    if (s->csv) fclose(s->csv);
    if (s->evcsv) fclose(s->evcsv);
    if (s->chcsv) fclose(s->chcsv);
    if (s->cfg.phy_mode == 1) phy_free(&s->phy);
}

// ----------------- Traffic + deadlines -----------------

static void arrivals(Sim *s) {
    // Bernoulli arrivals per UE
    for (int i = 0; i < s->cfg.num_ues; ++i) {
        if (rng_uniform01() < s->cfg.arrival_rate) {
            Packet p = {
                .bits = rng_int(s->cfg.pkt_bits_min, s->cfg.pkt_bits_max),
                .arrival_tti = s->tti,
                .deadline_tti = s->tti + s->cfg.deadline_ttis
            };
            (void)ue_queue_push(&s->ues[i], p);
            s->m.total_packets++;
        }
        // Legacy random-walk CQI only when PHY is disabled
        if (s->cfg.phy_mode == 0) {
            int delta = rng_int(-1, 1);
            s->ues[i].cqi += delta;
            if (s->ues[i].cqi < 1) s->ues[i].cqi = 1;
            if (s->ues[i].cqi > 15) s->ues[i].cqi = 15;
            s->ues[i].bprb_cur = 0; // not used
            s->ues[i].sinr_db_cur = 0;
            s->ues[i].rb_err_prob_cur = s->cfg.bler; // legacy uses BLER at TB level
        }
    }
}

static void expire_deadlines(Sim *s) {
    // Count misses if HoL packet's deadline is before now
    for (int i = 0; i < s->cfg.num_ues; ++i) {
        UE *u = &s->ues[i];
        Packet p;
        while (ue_queue_peek(u, &p) && p.deadline_tti < s->tti) {
            metrics_on_miss(&s->m, &p);
            u->pkts_missed++;
            ue_queue_pop(u);
        }
    }
}

// ----------------- HARQ ring helpers -----------------

static bool harq_enqueue(Sim *s, HarqEvent ev) {
    if (s->harq_count >= s->harq_cap) return false;
    s->harq_events[s->harq_tail] = ev;
    s->harq_tail = (s->harq_tail + 1) % s->harq_cap;
    s->harq_count++;
    return true;
}

static bool harq_peek_due(Sim *s, HarqEvent *out) {
    if (s->harq_count == 0) return false;
    HarqEvent *ev = &s->harq_events[s->harq_head];
    if (ev->tti_feedback != s->tti) return false;
    *out = *ev;
    return true;
}

static void harq_pop(Sim *s) {
    if (s->harq_count == 0) return;
    s->harq_head = (s->harq_head + 1) % s->harq_cap;
    s->harq_count--;
}

// Process all HARQ feedback events due at current TTI.
// If ACK -> count delivered; If NACK -> reinsert for retransmission.
static void process_harq_feedback(Sim *s) {
    HarqEvent ev;
    while (harq_peek_due(s, &ev)) {
        bool ack = false;

        if (s->cfg.phy_mode == 1) {
            // RB-level errors: ACK only if all RBs succeed
            double per = ev.rb_err_prob_at_tx;
            int rb = ev.rb_alloc > 0 ? ev.rb_alloc : 1;
            ack = true;
            for (int i = 0; i < rb; ++i) {
                if (rng_uniform01() < per) { ack = false; break; }
            }
        } else {
            // Legacy BLER at TB level
            double r = rng_uniform01();
            ack = (r > s->cfg.bler);
        }

        if (ack) {
            // Delivered at feedback time
            Packet tmp = { .bits = 0,
                           .arrival_tti = ev.pkt_arrival_tti,
                           .deadline_tti = ev.pkt_deadline_tti };
            metrics_on_deliver(&s->m, &tmp, s->tti, ev.pkt_size_bits);
            s->ues[ev.ue_id].pkts_delivered++;

            if (s->evcsv) {
                fprintf(s->evcsv, "%d,ACK,%d,%d,%d,%.2f,%d,%d,%.6f\n",
                        s->tti, ev.ue_id, ev.pkt_size_bits, ev.retx_count,
                        ev.sinr_db_at_tx, ev.cqi_at_tx, ev.rb_alloc, ev.rb_err_prob_at_tx);
            }
        } else {
            if (ev.retx_count >= 4) {
                // Drop after max retries
                Packet tmp = { .bits = ev.pkt_size_bits,
                               .arrival_tti = ev.pkt_arrival_tti,
                               .deadline_tti = ev.pkt_deadline_tti };
                metrics_on_miss(&s->m, &tmp);
                s->ues[ev.ue_id].pkts_missed++;

                if (s->evcsv) {
                    fprintf(s->evcsv, "%d,DROP,%d,%d,%d,%.2f,%d,%d,%.6f\n",
                            s->tti, ev.ue_id, ev.pkt_size_bits, ev.retx_count,
                            ev.sinr_db_at_tx, ev.cqi_at_tx, ev.rb_alloc, ev.rb_err_prob_at_tx);
                }
            } else {
                // NACK -> reinsert for retransmission (push-front)
                UE *u = &s->ues[ev.ue_id];
                if (u->q_count < MAX_QUEUE) {
                    u->q_head = (u->q_head - 1 + MAX_QUEUE) % MAX_QUEUE;
                    u->q[u->q_head] = (Packet){
                        .bits = ev.pkt_size_bits,
                        .arrival_tti = ev.pkt_arrival_tti,
                        .deadline_tti = ev.pkt_deadline_tti
                    };
                    u->q_count++;
                } else {
                    // queue full -> treat as miss
                    Packet tmp = { .bits = ev.pkt_size_bits,
                                   .arrival_tti = ev.pkt_arrival_tti,
                                   .deadline_tti = ev.pkt_deadline_tti };
                    metrics_on_miss(&s->m, &tmp);
                    s->ues[ev.ue_id].pkts_missed++;

                    if (s->evcsv) {
                        fprintf(s->evcsv, "%d,DROP,%d,%d,%d,%.2f,%d,%d,%.6f\n",
                                s->tti, ev.ue_id, ev.pkt_size_bits, ev.retx_count,
                                ev.sinr_db_at_tx, ev.cqi_at_tx, ev.rb_alloc, ev.rb_err_prob_at_tx);
                    }
                    harq_pop(s);
                    if (s->evcsv) fflush(s->evcsv);
                    continue;
                }

                if (s->evcsv) {
                    fprintf(s->evcsv, "%d,NACK,%d,%d,%d,%.2f,%d,%d,%.6f\n",
                            s->tti, ev.ue_id, ev.pkt_size_bits, ev.retx_count + 1,
                            ev.sinr_db_at_tx, ev.cqi_at_tx, ev.rb_alloc, ev.rb_err_prob_at_tx);
                }
            }
        }
        harq_pop(s);
        if (s->evcsv) fflush(s->evcsv);
    }
}

// ----------------- One TTI -----------------

void sim_step(Sim *s) {
    // Process ACK/NACKs arriving now
    process_harq_feedback(s);

    // Advance channel and take PHY snapshot for each UE
    if (s->cfg.phy_mode == 1) {
        phy_step(&s->phy, &s->cfg, s->tti);
        for (int u = 0; u < s->cfg.num_ues; ++u) {
            PhyUEInstant inst;
            phy_get_instant(&s->phy, &s->cfg, u, &inst);
            s->ues[u].cqi            = inst.cqi;
            s->ues[u].bprb_cur       = inst.bits_per_rb;
            s->ues[u].sinr_db_cur    = inst.sinr_db;
            s->ues[u].rb_err_prob_cur= inst.rb_err_prob;

            if (s->chcsv) {
                fprintf(s->chcsv, "%d,%d,%.2f,%d,%d,%.6f\n",
                        s->tti, u, inst.sinr_db, inst.cqi, inst.bits_per_rb, inst.rb_err_prob);
            }
        }
        if (s->chcsv) fflush(s->chcsv);
    }

    arrivals(s);
    expire_deadlines(s);

    // reset per-TTI debug flags
    for (int u = 0; u < s->cfg.num_ues; ++u) {
        s->ues[u].dbg_tx_bits_this_tti = 0;
        s->ues[u].dbg_was_scheduled    = 0;
    }

#if DEBUG_QUEUES
    printf("\n=== TTI %d: Before Scheduling ===\n", s->tti);
    for (int u = 0; u < s->cfg.num_ues; ++u) {
        UE *ue = &s->ues[u];
        printf("UE %02d: q=%d | deadlines(bits): ", u, ue->q_count);
        for (int k = 0, idx = ue->q_head; k < ue->q_count; ++k, idx = (idx + 1) % MAX_QUEUE) {
            Packet *pkt = &ue->q[idx];
            printf("%d(%d) ", pkt->deadline_tti, pkt->bits);
        }
        printf("\n");
    }
#endif

    int rb_used = 0;
    Completion comps[256];
    int comps_used = 0;

    int bits = schedule_edf(s->ues, s->cfg.num_ues, s->cfg.rb_total, s->tti,
                            &s->m, &rb_used,
                            comps, 256, &comps_used);

    s->m.total_bits_sent += bits;
    s->m.rb_used_total   += rb_used;

    // Convert completions into HARQ feedback events
    for (int i = 0; i < comps_used; ++i) {
        HarqEvent ev = {
            .ue_id = comps[i].ue_id,
            .tti_feedback = s->tti + s->cfg.harq_rtt,
            .pkt_arrival_tti = comps[i].pkt_arrival_tti,
            .pkt_deadline_tti = comps[i].pkt_deadline_tti,
            .pkt_size_bits = comps[i].pkt_size_bits,
            .retx_count = 0,

            .rb_alloc = comps[i].rb_alloc,
            .cqi_at_tx = comps[i].cqi_at_tx,
            .sinr_db_at_tx = comps[i].sinr_db_at_tx,
            .rb_err_prob_at_tx = comps[i].rb_err_prob_at_tx
        };
        (void)harq_enqueue(s, ev);
    }

    // CSV: log per-UE allocations for this TTI
    if (s->csv) {
        for (int u = 0; u < s->cfg.num_ues; ++u) {
            UE *ue = &s->ues[u];
            if (!ue->dbg_was_scheduled) continue;
            int bprb = (s->cfg.phy_mode==1 && ue->bprb_cur>0) ? ue->bprb_cur : bits_per_rb_for_cqi(ue->cqi);
            int rb_used_est = (bprb > 0) ? (ue->dbg_tx_bits_this_tti / bprb) : 0;

            int hol_deadline = 0;
            if (ue->q_count > 0) {
                Packet *p = &ue->q[ue->q_head];
                hol_deadline = p->deadline_tti;
            }

            fprintf(s->csv, "%d,%d,%d,%d,%d,%d,%d\n",
                    s->tti, u, ue->dbg_tx_bits_this_tti, rb_used_est,
                    ue->cqi, ue->q_count, hol_deadline);
        }
        fflush(s->csv);
    }

#if DEBUG_QUEUES
    printf("=== TTI %d: Packets Sent ===\n", s->tti);
    for (int u = 0; u < s->cfg.num_ues; ++u) {
        UE *ue = &s->ues[u];
        if (ue->dbg_was_scheduled) {
            printf("UE %02d sent %d bits\n", u, ue->dbg_tx_bits_this_tti);
        }
    }
    printf("=== TTI %d: After Scheduling ===\n", s->tti);
    for (int u = 0; u < s->cfg.num_ues; ++u) {
        UE *ue = &s->ues[u];
        printf("UE %02d: q=%d | deadlines(bits): ", u, ue->q_count);
        for (int k = 0, idx = ue->q_head; k < ue->q_count; ++k, idx = (idx + 1) % MAX_QUEUE) {
            Packet *pkt = &ue->q[idx];
            printf("%d(%d) ", pkt->deadline_tti, pkt->bits);
        }
        printf("\n");
    }
#endif
}

// ----------------- Run + summary -----------------

void sim_run(Sim *s) {
    for (s->tti = 0; s->tti < s->cfg.ttis; ++s->tti) {
        sim_step(s);
    }
}

void sim_print_summary(const Sim *s) {
    double miss_rate = (s->m.total_packets == 0) ? 0.0 :
                       (double)s->m.deadline_misses / (double)s->m.total_packets;
    double avg_latency = (s->m.total_packets - s->m.deadline_misses) > 0
        ? (double)s->m.sum_latency / (double)(s->m.total_packets - s->m.deadline_misses)
        : 0.0;

    printf("=== L1 Scheduler Summary ===\n");
    printf("TTIs: %d, UEs: %d, RB/TTI: %d\n", s->cfg.ttis, s->cfg.num_ues, s->cfg.rb_total);
    printf("Arrivals: %lld pkts\n", s->m.total_packets);
    printf("Bits sent: %lld bits (%.2f Mbits)\n", s->m.total_bits_sent, s->m.total_bits_sent / 1e6);
    printf("Deadline misses: %lld (%.2f%%)\n", s->m.deadline_misses, miss_rate * 100.0);
    printf("Avg latency (TTIs) over delivered: %.2f\n", avg_latency);
    double util = (double)s->m.rb_used_total / ((double)s->cfg.ttis * (double)s->cfg.rb_total);
    printf("RB utilization: %.2f%%\n", util * 100.0);
}
