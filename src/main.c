#include "common.h"
#include "sim.h"

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s --ttis N --rb N --ues N [options]\n"
        "\n"
        "Required:\n"
        "  --ttis N           total TTIs to simulate (e.g., 10000)\n"
        "  --rb N             resource blocks per TTI (capacity)\n"
        "  --ues N            number of UEs\n"
        "\n"
        "Traffic / deadlines:\n"
        "  --arrival P        arrival prob per UE per TTI (default 0.2)\n"
        "  --deadline D       relative deadline in TTIs (default 8)\n"
        "  --seed S           RNG seed (default 42)\n"
        "\n"
        "HARQ (legacy BLER path):\n"
        "  --bler P           BLER (0..1) for HARQ (default 0.1)\n"
        "  --harq N           HARQ RTT in TTIs (default 8)\n"
        "\n"
        "Output:\n"
        "  --csv PATH         write per-TTI allocations to CSV file\n"
        "\n"
        "PHY / channel model (set --phy-mode 1 to enable):\n"
        "  --phy-mode M       0=legacy (default), 1=channel-based with RB errors\n"
        "  --pathloss-exp X   path loss exponent (default 3.5)\n"
        "  --shadowing-std X  shadowing std dev in dB (default 6.0)\n"
        "  --fading-rho X     AR(1) fast-fading correlation 0..1 (default 0.9)\n"
        "  --snr-ref X        reference (median) SNR in dB (default 18.0)\n"
        "  --rb-floor-perr X  minimum per-RB error probability (default 1e-4)\n"
        "\n"
        "Notes:\n"
        "  * When --phy-mode 1 is used, HARQ ACK/NACK is driven by RB-level errors.\n"
        "    The --bler value is ignored in that mode.\n",
        argv0);
}

int main(int argc, char **argv) {
    Config cfg = {
        .ttis = -1,
        .rb_total = -1,
        .num_ues = -1,
        .seed = 42,
        .arrival_rate = 0.2,
        .pkt_bits_min = 800,    // ~100 bytes
        .pkt_bits_max = 12000,  // ~1500 bytes
        .deadline_ttis = 8,
        .bler = 0.1,
        .harq_rtt = 8,
        .out_dir = NULL,
        .csv_path = NULL,
        
        // PHY Defaults
        .phy_mode = 0, 
        .pathloss_exp = 3.5,
        .shadowing_std_db = 6.0,
        .fading_rho = 0.9,
        .snr_ref_db = 18.0,
        .rb_floor_perr = 1e-4
    };

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--ttis") && i+1 < argc) cfg.ttis = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rb") && i+1 < argc) cfg.rb_total = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ues") && i+1 < argc) cfg.num_ues = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--arrival") && i+1 < argc) cfg.arrival_rate = atof(argv[++i]);
        else if (!strcmp(argv[i], "--deadline") && i+1 < argc) cfg.deadline_ttis = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i+1 < argc) cfg.seed = (unsigned int)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--bler") && i+1 < argc) cfg.bler = atof(argv[++i]);
        else if (!strcmp(argv[i], "--harq") && i+1 < argc) cfg.harq_rtt = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--csv") && i+1 < argc) cfg.csv_path = argv[++i];

        // PHY / channel args
        else if (!strcmp(argv[i], "--phy-mode") && i+1 < argc) cfg.phy_mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pathloss-exp") && i+1 < argc) cfg.pathloss_exp = atof(argv[++i]);
        else if (!strcmp(argv[i], "--shadowing-std") && i+1 < argc) cfg.shadowing_std_db = atof(argv[++i]);
        else if (!strcmp(argv[i], "--fading-rho") && i+1 < argc) cfg.fading_rho = atof(argv[++i]);
        else if (!strcmp(argv[i], "--snr-ref") && i+1 < argc) cfg.snr_ref_db = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rb-floor-perr") && i+1 < argc) cfg.rb_floor_perr = atof(argv[++i]);
        else {
            usage(argv[0]);
            return 1;
        }
    }

    if (cfg.ttis <= 0 || cfg.rb_total <= 0 || cfg.num_ues <= 0) {
        usage(argv[0]);
        return 1;
    }

    // Clamp some PHY params to sane ranges
    if (cfg.fading_rho < 0.0) cfg.fading_rho = 0.0;
    if (cfg.fading_rho > 0.999) cfg.fading_rho = 0.999;
    if (cfg.rb_floor_perr < 0.0) cfg.rb_floor_perr = 0.0;
    if (cfg.rb_floor_perr > 1.0) cfg.rb_floor_perr = 1.0;

    rng_seed(cfg.seed);

    if (cfg.phy_mode == 1 && cfg.bler != 0.1) {
        fprintf(stderr, "[info] PHY mode enabled: --bler is ignored (using RB-level errors)\n");
    }

    Sim sim = {0};
    sim_init(&sim, &cfg);
    sim_run(&sim);
    sim_print_summary(&sim);
    sim_free(&sim);
    return 0;
}
