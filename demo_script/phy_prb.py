#!/usr/bin/env python3

import io
import time
import matplotlib.pyplot as plt
import matplotlib as mpl
import matplotlib.animation
import numpy as np
import argparse

parser = argparse.ArgumentParser()

parser.add_argument("-i", "--interval", type=int, help="Animation interval in ms.")
args = parser.parse_args()

BUFFER_LEN = 64
DATA_FILENAME = "/home/wanhr/Documents/codes/cpp/NG-Scope-5G/build/nrscope/src/a.csv"
PLOT_LIMIT = 20
ANIM_FILENAME = "video.gif"

INTERVAL = args.interval #ms
SLOT_TIME = 0.5
DL_TOTAL = 0.8
PRB_NUM = INTERVAL / SLOT_TIME * DL_TOTAL * 51 * 12
print(PRB_NUM)

file_cur = 1
first_time = 0
ue_list = dict()
ue_id = 0

fig, ax = plt.subplots(1, 1)#, figsize=(10,8))
ax.set_title("PRB Percentage from NR-Scope")
ax.set_xlabel("Time (sec)")
ax.set_ylabel("PRB Percentage (%)")

def get_data(filename, buffer_len, delay=0.0):
    with open(filename, "r") as f:
        data = f.readlines()
        if delay:
            time.sleep(delay)
    return data


def animate(i, xs, ys, limit=PLOT_LIMIT, verbose=False):
    global file_cur, first_time, ue_list, ue_id
    # grab the data
    # try:
    data = get_data(DATA_FILENAME, BUFFER_LEN)
    if verbose:
        print(data)
    while(data[file_cur].split(',')[0] == 'timestamp'):
        file_cur = file_cur + 1
    first_time = float(data[file_cur].split(',')[0])
    # print(file_cur, first_time)
    prb_data = [[] for ue_i in range(len(ue_list))]
    for line_id in range(0, len(data) - file_cur):
        data_line = data[file_cur+line_id].split(',')
        if (data_line[0] == 'timestamp'): 
            # File header of split of file
            continue
        if (data_line[3] not in ue_list): 
            # add new UE into the list
            ue_list[data_line[3]] = ue_id
            ue_id = ue_id + 1
            prb_data.append([])
        if (float(data_line[0]) - first_time < INTERVAL/1000 and 
            data_line[5] == "1_1"):
            #
            prb_data[ue_list[data_line[3]]].append(int(data_line[9]) * int(data_line[11]))
        if (float(data_line[0]) - first_time >= INTERVAL/1000):
            file_cur = line_id + file_cur
            break
        
    first_time = first_time + 1
    ts = i * INTERVAL / 1000
    
    # x, y = map(float, data.split())
    if len(xs) == 0 or ts > xs[-1]:
        # Add x and y to lists
        xs.append(ts)
        if (ue_id > len(ys)):
            ys.append([])
        for ue_i in range(ue_id):
            ys[ue_i].append(np.sum(prb_data[ue_i]) / PRB_NUM * 100)
        # Limit x and y lists to 10 items
        # xs = xs[-limit:]
        # ys = ys[-limit:]
    else:
        print(f"W: {time.time()} :: STALE!")
    # except ValueError:
    #     print(f"W: {time.time()} :: EXCEPTION!")
    # else:
    # Draw x and y lists
    ax.clear()
    ax.set_title("PRB Percentage from NR-Scope")
    ax.set_xlabel("Time (sec)")
    ax.set_ylabel("PRB Percentage (%)")
    ax.set_ylim([0, 100])
    ax.bar(xs[(len(xs) - len(ys[0])):], ys[0], label="UE {}".format(0))
    for ue_i in range(1, ue_id):
        ax.bar(xs[(len(xs) - len(ys[ue_i])):], ys[ue_i], 
               bottom=ys[ue_i-1][(len(ys[ue_i-1]) - len(ys[ue_i])):], 
               label="UE {}".format(ue_i))
    ax.legend()


# save video (only to attach here) 
#anim = mpl.animation.FuncAnimation(fig, animate, fargs=([time.time()], [None]), interval=1, frames=3 * PLOT_LIMIT, repeat=False)
#anim.save(ANIM_FILENAME, writer='imagemagick', fps=10)
#print(f"I: Saved to `{ANIM_FILENAME}`")

# show interactively
anim = mpl.animation.FuncAnimation(fig, animate, fargs=([], [[]]), interval=INTERVAL)
plt.show()
plt.close()