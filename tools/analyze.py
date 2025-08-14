#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
import sys, os

path = sys.argv[1] if len(sys.argv) > 1 else "data/schedule.csv"
if not os.path.exists(path):
    raise SystemExit(f"CSV not found: {path}")

df = pd.read_csv(path)
if df.empty:
    raise SystemExit("CSV is empty")

# ---------- 1) Total RB utilization per TTI ----------
util = df.groupby("tti")["rb_used"].sum()
plt.figure()
util.plot()
plt.xlabel("TTI")
plt.ylabel("RBs used")
plt.title("Total RB utilization per TTI")
plt.tight_layout()
plt.savefig("data/plot_utilization.png")

# ---------- 2) Per-UE RBs (first 6 UEs) ----------
sample = sorted(df["ue"].unique())[:6]
pivot = df[df["ue"].isin(sample)].pivot_table(
    index="tti", columns="ue", values="rb_used", aggfunc="sum", fill_value=0
)
plt.figure()
pivot.plot()
plt.xlabel("TTI")
plt.ylabel("RBs used")
plt.title("Per-UE RB allocation (sample)")
plt.tight_layout()
plt.savefig("data/plot_per_ue.png")

# ---------- 3) Queue pressure (avg queue len after scheduling) ----------
qavg = df.groupby("tti")["queue_after"].mean()
plt.figure()
qavg.plot()
plt.xlabel("TTI")
plt.ylabel("Avg queue length (post-sched)")
plt.title("Queue pressure over time")
plt.tight_layout()
plt.savefig("data/plot_queue_pressure.png")

print("Wrote:")
print("  data/plot_utilization.png")
print("  data/plot_per_ue.png")
print("  data/plot_queue_pressure.png")

# ---------- 4) HARQ events ----------
ev_path = "data/events.csv"
if os.path.exists(ev_path):
    ev = pd.read_csv(ev_path)
    if not ev.empty:
        counts = ev.groupby(["tti","event"]).size().unstack(fill_value=0)
        plt.figure()
        counts.plot()
        plt.xlabel("TTI")
        plt.ylabel("Count")
        plt.title("HARQ events per TTI (ACK/NACK/DROP)")
        plt.tight_layout()
        plt.savefig("data/plot_harq_events.png")
        print("  data/plot_harq_events.png")

# ---------- 5) Channel snapshots (if present) ----------
ch_path = "data/channel.csv"
if os.path.exists(ch_path):
    ch = pd.read_csv(ch_path)
    if not ch.empty:
        # SINR per-UE (first 6 UEs)
        ch_sample = ch[ch["ue"].isin(sample)]
        pivot_sinr = ch_sample.pivot_table(index="tti", columns="ue", values="sinr_db", aggfunc="last")
        plt.figure()
        pivot_sinr.plot()
        plt.xlabel("TTI")
        plt.ylabel("SINR (dB)")
        plt.title("Per-UE SINR over time (sample)")
        plt.tight_layout()
        plt.savefig("data/plot_sinr_per_ue.png")
        print("  data/plot_sinr_per_ue.png")

        # CQI vs SINR scatter
        plt.figure()
        ch.plot.scatter(x="sinr_db", y="cqi", alpha=0.3)
        plt.xlabel("SINR (dB)")
        plt.ylabel("CQI")
        plt.title("CQI vs SINR")
        plt.tight_layout()
        plt.savefig("data/plot_cqi_vs_sinr.png")
        print("  data/plot_cqi_vs_sinr.png")

        # RB error prob and NACK overlay (per TTI)
        if os.path.exists(ev_path):
            ev = pd.read_csv(ev_path)
            if not ev.empty:
                nack = ev[ev["event"]=="NACK"].groupby("tti").size()
                perr = ch.groupby("tti")["rb_err_prob"].mean()
                ax = perr.plot()
                ax2 = ax.twinx()
                nack.plot(ax=ax2, style=".", label="NACK count")
                ax.set_xlabel("TTI")
                ax.set_ylabel("Avg RB error prob")
                ax2.set_ylabel("NACKs")
                ax.set_title("RB error probability vs NACKs")
                plt.tight_layout()
                plt.savefig("data/plot_perr_vs_nacks.png")
                print("  data/plot_perr_vs_nacks.png")
