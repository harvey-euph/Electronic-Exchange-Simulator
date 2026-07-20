#!/usr/bin/env python3
import sqlite3
import argparse
import sys
import os
import tty
import termios

parser = argparse.ArgumentParser(description="Interactive trace tree viewer from SQLite DB")
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
    10: "resp_enqueue (Point in time)",
    11: "resp_reserve",
    12: "resp_new",
    13: "resp_commit",
    14: "sched_out (context switched out)",
    15: "sched_in (context switched in)",
    16: "page_fault",
    17: "createOrder",
    18: "hard_irq",
    19: "softirq",
    20: "pmu_stats",
}

syscall_names = {}
try:
    with open('/usr/include/x86_64-linux-gnu/asm/unistd_64.h', 'r') as f:
        for line in f:
            if line.startswith('#define __NR_'):
                parts = line.split()
                if len(parts) >= 3:
                    syscall_names[int(parts[2])] = parts[1][5:]
except:
    pass

cursor = conn.cursor()
cursor.execute("SELECT exec_id, event_type, is_start, ts, map_name, pmu_l1, pmu_llc, pmu_branch FROM events ORDER BY ts ASC")
all_events = cursor.fetchall()

if not all_events:
    print("No trace events found in database.")
    sys.exit(0)

# Split events into individual runs (each processRequest is a run)
runs = []
current_run = []
for row in all_events:
    is_process_req = (row[1] == 0) or (row[1] == 999 and row[4] == "processRequest")
    if is_process_req and row[2] == 1 and len(current_run) > 0:
        runs.append(current_run)
        current_run = []
    current_run.append(row)
if current_run:
    runs.append(current_run)

def getch():
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setcbreak(sys.stdin.fileno())
        ch = sys.stdin.read(1)
        if ch == '\x1b': # Handle arrow keys
            ch2 = sys.stdin.read(2)
            if ch2 == '[C': return 'n' # Right arrow
            if ch2 == '[D': return 'p' # Left arrow
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return ch.lower()

idx = 0
total = len(runs)

while True:
    run_events = runs[idx]
    exec_id = run_events[0][0]
    
    # Clear screen for better interactive feel
    os.system('clear' if os.name == 'posix' else 'cls')
    
    print(f"--- Trace Tree Viewer ---")
    print(f"Run {idx + 1} of {total} (exec_id: {exec_id})")
    print("   Total time (+  Diff time) (  L1,  LLC, Branch) Misses")
    print("-" * 80)
    
    first_ts = run_events[0][3]
    last_event_ts = first_ts
    last_pmu = None
    stack = []
    last_resp_ts = None
    
    for row in run_events:
        exec_id, etype, is_start, ts, map_name, pmu_l1, pmu_llc, pmu_branch = row
        
        if etype not in event_names and etype != 999 and etype < 10000:
            continue
            
        if etype >= 10000:
            sys_id = etype - 10000
            name = "sys_" + syscall_names.get(sys_id, str(sys_id))
        elif etype == 999:
            name = row[4]
        else:
            name = event_names.get(etype, f"UNKNOWN_{etype}")
            if map_name:
                name = f"{name} ({map_name})"
                
        if not name or name in ("sys_uretprobe", "sys_uprobe", "GHOST_ENDED"):
            continue
            
        diff_l1 = 0
        diff_llc = 0
        diff_branch = 0
        
        if last_pmu is not None:
            diff_l1 = max(0, pmu_l1 - last_pmu[0])
            diff_llc = max(0, pmu_llc - last_pmu[1])
            diff_branch = max(0, pmu_branch - last_pmu[2])
            
        last_pmu = (pmu_l1, pmu_llc, pmu_branch)
            
        delta_us = (ts - first_ts) / 1000.0
        diff_us = (ts - last_event_ts) / 1000.0
        last_event_ts = ts
        
        prefix = f"{delta_us:10.3f} us (+{diff_us:8.3f} us) ({diff_l1:4d}, {diff_llc:4d}, {diff_branch:4d})"
            
        if etype in (10, 11, 12, 13):
            indent = "  " * len(stack)
            if etype == 11:
                print(f"{prefix} {indent}-> resp_enqueue_start")
            elif etype == 12:
                cost = (ts - last_resp_ts) if last_resp_ts else 0
                print(f"{prefix} {indent}-> resp_reserved (took {cost} ns)")
            elif etype == 13:
                cost = (ts - last_resp_ts) if last_resp_ts else 0
                print(f"{prefix} {indent}-> resp_writed (took {cost} ns)")
            elif etype == 10:
                cost = (ts - last_resp_ts) if last_resp_ts else 0
                print(f"{prefix} {indent}-> resp_commited (took {cost} ns)")
            last_resp_ts = ts
            continue
        
        if etype in (14, 15):
            if etype == 14:
                print(f"\033[91m{prefix} === {name} ===\033[0m")
            elif etype == 15:
                print(f"\033[91m{prefix} === {name} ===\033[0m")
            continue
            
        color_start = "\033[91m" if etype in (16, 18, 19) else ("\033[93m" if etype >= 10000 else "")
        color_end = "\033[0m" if etype in (16, 18, 19) or etype >= 10000 else ""
        
        if is_start == 1:
            indent = "  " * len(stack)
            print(f"{color_start}{prefix} {indent}-> {name} started{color_end}")
            stack.append((etype, name, ts))
        else:
            # Find the matching start event in the stack
            match_idx = -1
            for i in range(len(stack) - 1, -1, -1):
                if stack[i][0] == etype and (etype != 999 or stack[i][1] == name):
                    match_idx = i
                    break
            
            if match_idx != -1:
                start_etype, start_name, start_ts = stack.pop(match_idx)
                duration_ns = ts - start_ts
                indent = "  " * len(stack)
                print(f"{color_start}{prefix} {indent}<- {name} ended (took {duration_ns} ns){color_end}")
            else:
                indent = "  " * len(stack)
                print(f"{color_start}{prefix} {indent}<- {name} ended{color_end}")
                
            if name == "processRequest":
                break
    print("-" * 80)
    print("Use [n] or [Right Arrow] for Next")
    print("Use [p] or [Left Arrow]  for Previous")
    print("Use [q] to Quit")
    
    while True:
        sys.stdout.write("Action > ")
        sys.stdout.flush()
        action = getch()
        print()
        
        if action == 'n' and idx < total - 1:
            idx += 1
            break
        elif action == 'p' and idx > 0:
            idx -= 1
            break
        elif action == 'q':
            sys.exit(0)
        else:
            if action == 'n': print("Already at the last run.")
            elif action == 'p': print("Already at the first run.")

conn.close()
