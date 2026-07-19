#!/usr/bin/env python3
import sqlite3
import argparse
import sys
import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description="Analyze trace events from SQLite DB for latency statistics")
parser.add_argument("-d", "--db", type=str, default="traces.db", help="Path to traces.db")
args = parser.parse_args()

if not os.path.exists(args.db):
    print(f"Error: Database {args.db} not found.")
    sys.exit(1)

conn = sqlite3.connect(args.db)

event_names = {
    0: "processRequest",
    1: "match",
    2: "match_outer_loop",
    3: "match_inner_loop",
    4: "map_find",
    5: "map_insert",
    6: "map_erase",
    7: "handleCancel",
    8: "handleModify",
    9: "handleNew",
    10: "Latency to First Response",
    11: "resp_reserve",
    12: "resp_new",
    13: "resp_commit",
    14: "sched_out (context switched out)",
    15: "sched_in (context switched in)",
    16: "page_fault",
    17: "createOrder",
    18: "hard_irq",
    19: "softirq",
}

print(f"Loading trace events from {args.db}...")
df = pd.read_sql_query("SELECT * FROM events ORDER BY ts", conn)
conn.close()

if df.empty:
    print("No trace events found in database.")
    sys.exit(0)

print(f"Loaded {len(df)} events. Analyzing...")

latencies = {k: [] for k in event_names.keys()}
pmu_stats = {'L1': [], 'LLC': [], 'Branch': []}

for exec_id, group in df.groupby('exec_id'):
    group = group.sort_values('ts')
    
    stacks = {k: [] for k in event_names.keys()}
    req_start_ts = None
    
    for _, row in group.iterrows():
        etype = row['event_type']
        if etype not in event_names:
            continue

        if etype == 0 and row['is_start'] == 1:
            req_start_ts = row['ts']
            
        if etype in (10, 11, 12, 13, 14, 15) and row['is_start'] == 1:
            if req_start_ts is not None:
                latencies[etype].append(row['ts'] - req_start_ts)
            continue
            
        if row['is_start'] == 1:
            stacks[etype].append(row['ts'])
        else:
            if len(stacks[etype]) > 0:
                start_ts = stacks[etype].pop()
                latencies[etype].append(row['ts'] - start_ts)
                
    if len(group) >= 2:
        first_row = group.iloc[0]
        last_row = group.iloc[-1]
        pmu_stats['L1'].append(max(0, last_row['pmu_l1'] - first_row['pmu_l1']))
        pmu_stats['LLC'].append(max(0, last_row['pmu_llc'] - first_row['pmu_llc']))
        pmu_stats['Branch'].append(max(0, last_row['pmu_branch'] - first_row['pmu_branch']))

print("\n")
print(f"{'Component':<35} | {'Samples':<10} | {'p90 (ns)':<12} | {'p99 (ns)':<12} | {'p99.9 (ns)':<12}")
print("-" * 85)

for etype, name in event_names.items():
    lats = latencies[etype]
    if len(lats) > 0:
        p90 = np.percentile(lats, 90)
        p99 = np.percentile(lats, 99)
        p999 = np.percentile(lats, 99.9)
        print(f"{name:<35} | {len(lats):<10} | {p90:<12.0f} | {p99:<12.0f} | {p999:<12.0f}")

print("\n")
print(f"{'PMU HW Counters (Misses per Req)':<35} | {'Samples':<10} | {'p90':<12} | {'p99':<12} | {'p99.9':<12}")
print("-" * 85)
for metric, values in pmu_stats.items():
    if len(values) > 0:
        p90 = np.percentile(values, 90)
        p99 = np.percentile(values, 99)
        p999 = np.percentile(values, 99.9)
        print(f"{metric + ' Miss':<35} | {len(values):<10} | {p90:<12.0f} | {p99:<12.0f} | {p999:<12.0f}")

plot_types = [0, 1, 2, 3, 4, 6] 
fig, axes = plt.subplots(len(plot_types), 1, figsize=(10, 15))
fig.tight_layout(pad=5.0)

for i, etype in enumerate(plot_types):
    ax = axes[i]
    lats = latencies[etype]
    if len(lats) > 0:
        p99 = np.percentile(lats, 99)
        filtered = [x for x in lats if x <= p99]
        if len(filtered) > 0:
            ax.hist(filtered, bins=50, color='skyblue', edgecolor='black')
        ax.set_title(f"Latency Distribution: {event_names[etype]} (up to p99)")
        ax.set_xlabel("Latency (ns)")
        ax.set_ylabel("Frequency")

plt.savefig("latency_analysis.png")
print("\nGenerated latency distribution plots in latency_analysis.png")
