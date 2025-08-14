# L1 Scheduler Simulation
A C-based testbed for simulating a MAC scheduler at the base station (gNB/eNB) side, now with a lightweight PHY abstraction (pathloss, shadowing, fading, RB error model).
Models per-UE queues, packet arrivals, deadlines, CQI/MCS mapping, HARQ, and realistic channel impairments. Includes Python scripts for visualization.

# Quickstart (WSL/Ubuntu 22.04)
sudo apt update
sudo apt install -y build-essential git python3 python3-pip
git clone https://github.com/yourusername/yourrepo.git
cd yourrepo
make clean && make

# Run Simulation
./bin/l1sched \
  --ttis 2000 --rb 100 --ues 32 \
  --arrival 0.2 --deadline 8 \
  --phy-mode 1 \
  --pathloss-exp 3.5 \
  --shadowing-std 6.0 \
  --fading-rho 0.9 \
  --snr-ref 18.0 \
  --rb-floor-perr 1e-4 \
  --harq 8 \
  --csv data/schedule.csv

./bin/l1sched \
  --ttis 10000 --rb 200 --ues 100 \
  --arrival 0.2 --deadline 8 \
  --phy-mode 1 \
  --pathloss-exp 3.5 \
  --shadowing-std 6.0 \
  --fading-rho 0.9 \
  --snr-ref 18.0 \
  --rb-floor-perr 1e-4 \
  --harq 8 \
  --csv data/schedule_med.csv

(larger scale)

# Analyzing Results
python3 tools/analyze.py data/schedule.csv

plot_utilization.png — RB utilization per TTI

plot_per_ue.png — RB allocation for sample UEs

plot_queue_pressure.png — average queue length

plot_harq_events.png — ACK/NACK/DROP counts

plot_sinr_per_ue.png — SINR evolution per UE

plot_cqi_vs_sinr.png — CQI vs. SINR scatter

plot_perr_vs_nacks.png — error rate vs. retransmissions


File Overview:
src/main.c	CLI argument parsing, sets up config, runs simulation, prints summary.
src/sim.c	Core simulation loop: packet arrivals, CQI updates, deadline expiry, HARQ feedback handling, CSV logging.
src/scheduler.c	EDF scheduler implementation, CQI→bits/RB mapping, RB allocation logic.
src/phy.c	Lightweight PHY/channel model: pathloss, shadowing, fading, SNR→PER mapping, RB error injection.
src/metrics.c	Metrics collection: throughput, latency, misses, RB utilization.
inc/common.h	Common structs (UE, Packet, Config, Metrics) and utility functions.
inc/phy.h	PHY model function declarations.
tools/analyze.py	Reads CSV output, generates performance and channel plots.


Usage:

Compile:
make clean && make

Example Use:
./bin/l1sched --ttis 2000 --rb 100 --ues 32 --arrival 0.2 --deadline 8 --seed 42

Reduce load (same deadline)
./bin/l1sched --ttis 2000 --rb 100 --ues 32 --arrival 0.1 --deadline 8 --seed 42