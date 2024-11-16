#!/usr/bin/env python3


import io
import time
import matplotlib
import matplotlib.pyplot as plt
import matplotlib as mpl
import matplotlib.animation
import numpy as np
import argparse

from matplotlib.ticker import FormatStrFormatter

parser = argparse.ArgumentParser()

parser.add_argument("-i", "--interval", type=int, help="Animation interval in ms.")
args = parser.parse_args()

BUFFER_LEN = 64
DATA_FILENAME = "t_mobile2_ueactivity_afternoon_3.csv"
ANIM_FILENAME = "video.gif"

INTERVAL = args.interval #ms
PLOT_LIMIT = 20 * INTERVAL # 5 seconds

SLOT_TIME = 1
DL_TOTAL = 0.8
PRB_NUM = INTERVAL / SLOT_TIME * DL_TOTAL * 51 * 12

FONT_SIZE = 25

file_cur = 1
first_time = 0
ue_list = dict()
ue_id = 0
data = []

# matplotlib.use('TkAgg')
fig = plt.figure(0, figsize=(16,9))
# plt.rcParams["text.usetex"] = True
# plt.rcParams["font.weight"] = "light"
plt.rcParams["font.size"] = FONT_SIZE
# plt.rcParams["font.family"] = "Times"

ax1 = fig.add_subplot(7, 1, 1)
ax1.set_xticklabels([])
ax1.set_ylabel("DL Tput\n (Mbit/s)")

ax2 = fig.add_subplot(7, 1, 2)
ax2.set_xticklabels([])
ax2.set_ylabel("UL Tput\n (Mbit/s)")

ax3 = fig.add_subplot(7, 1, 3)
ax3.set_xticklabels([])
ax3.set_ylabel("PRB (%)")

ax4 = fig.add_subplot(7, 1, 4)
ax4.set_xticklabels([])
ax4.set_ylabel("MCS")

ax5 = fig.add_subplot(7, 1, 5)
ax5.set_xticklabels([])
ax5.set_ylabel("ReTXs")

ax6 = fig.add_subplot(7, 1, 6)
ax6.set_xticklabels([])
ax6.set_ylabel("TPC")

ax7 = fig.add_subplot(7, 1, 7)
ax7.set_xticklabels([])
ax7.set_ylabel("MIMO\nLayers")

ndi_data = [[[] for harq_id in range(16)] for ue_i in range(len(ue_list))]

color_layers = [
    "#f55702",
    "#ff00d4",
    "#9200fa",
    "#1500ff",
    "#ff0000",
    "#f2e200",
    "#6aff00",
    "#05499c",
    "#b56d00"] # 9 elements

def get_data(filename, delay=0.0):
    print("getting data")
    with open(filename, "r") as f:
        data = f.readlines()
        if delay:
            time.sleep(delay)
    return data

def animate(i, xs, ys, limit=PLOT_LIMIT, verbose=False):
    global file_cur, first_time, ue_list, ue_id, data
    # grab the data
    # try:
    start_time = time.time()
    if (len(data) == 0):
        data = get_data(DATA_FILENAME)
    if verbose:
        print(data)
    while(data[file_cur].split(',')[0] == 'timestamp'):
        file_cur = file_cur + 1
    if (float(data[-1].split(',')[0]) - float(data[file_cur].split(',')[0]) <= 1):
        data = get_data(DATA_FILENAME)
    first_time = float(data[file_cur].split(',')[0])
    # print(file_cur, first_time)
    dl_tbs_data = [[] for ue_i in range(len(ue_list))]
    ul_tbs_data = [[] for ue_i in range(len(ue_list))]
    prb_data = [[] for ue_i in range(len(ue_list))]
    mcs_data = [[] for ue_i in range(len(ue_list))]
    retx_data = [[] for ue_i in range(len(ue_list))]
    tpc_data = [[] for ue_i in range(len(ue_list))]
    mimo_data = [[] for ue_i in range(len(ue_list))]
    for line_id in range(0, len(data) - file_cur):
        data_line = data[file_cur+line_id].split(',')
        # print(data_line)
        if (data_line[0] == 'timestamp'):
            continue
        if (data_line[3] not in ue_list):
            ue_list[data_line[3]] = ue_id
            ue_id = ue_id + 1
            dl_tbs_data.append([0])
            ul_tbs_data.append([0])
            prb_data.append([0])
            mcs_data.append([0])
            ndi_data.append([[0] for harq_id in range(16)])
            retx_data.append([0])
            tpc_data.append([0])
            mimo_data.append([0])
        if (float(data_line[0]) - first_time < INTERVAL/1000 and 
            data_line[5] == "1_1"):
            dl_tbs_data[ue_list[data_line[3]]].append(int(data_line[19]))
            prb_data[ue_list[data_line[3]]].append(int(data_line[9]) * int(data_line[11]))
            ndi_data[ue_list[data_line[3]]][int(data_line[27])].append(int(data_line[22]))
            mcs_data[ue_list[data_line[3]]].append(int(data_line[18]))
            tpc_data[ue_list[data_line[3]]].append(int(data_line[29]))
            mimo_data[ue_list[data_line[3]]].append(int(data_line[14]))
        if (float(data_line[0]) - first_time < INTERVAL/1000 and 
            data_line[5] == "0_1"):
            ul_tbs_data[ue_list[data_line[3]]].append(int(data_line[19]))
        if (float(data_line[0]) - first_time >= INTERVAL/1000):
            file_cur = line_id + file_cur
            break
        
    for ue_i in range(len(ue_list)):
        retx_count = 0
        for harq_id in range(16):
            for slot_i in range(1, len(ndi_data[ue_i][harq_id])):
                if (ndi_data[ue_i][harq_id][slot_i] == ndi_data[ue_i][harq_id][slot_i-1]):
                    retx_count = retx_count + 1
        retx_data[ue_i] = retx_count
        
    first_time = first_time + 1
    ts = i * INTERVAL / 1000
    
    if len(xs[0]) == 0 or ts > xs[0][-1]:
        # First row, dl throughput
        xs[0].append(ts)
        while (ue_id > len(ys[0])):
            ys[0].append([])
        for ue_i in range(ue_id):
            if(len(dl_tbs_data[ue_i]) > 0):
                ys[0][ue_i].append(np.sum(dl_tbs_data[ue_i]) / INTERVAL * 1000 / 1000000)
            else:
                ys[0][ue_i].append(0)
    else:
        print(f"W: {time.time()} :: STALE!")

    if len(xs[1]) == 0 or ts > xs[1][-1]:
        # Second row, ul throughput
        xs[1].append(ts)
        while (ue_id > len(ys[1])):
            ys[1].append([])
        for ue_i in range(ue_id):
            if (len(ul_tbs_data[ue_i]) > 0):
                ys[1][ue_i].append(np.sum(ul_tbs_data[ue_i]) / INTERVAL * 1000 / 1000000)
            else:
                ys[1][ue_i].append(0)
    else:
        print(f"W: {time.time()} :: STALE!")

    if len(xs[2]) == 0 or ts > xs[2][-1]:
        # Second row, ul throughput
        xs[2].append(ts)
        while (ue_id > len(ys[2])):
            ys[2].append([])
        for ue_i in range(ue_id):
            if (len(prb_data[ue_i]) > 0 ):
                ys[2][ue_i].append(np.sum(prb_data[ue_i]) / PRB_NUM * 100)
            else:
                ys[2][ue_i].append(0)
    else:
        print(f"W: {time.time()} :: STALE!")

    if len(xs[3]) == 0 or ts > xs[3][-1]:
        # Add x and y to lists
        xs[3].append(ts)
        while (ue_id > len(ys[3])):
            ys[3].append([])
        for ue_i in range(ue_id):
            if (len(mcs_data[ue_i]) > 0):
                ys[3][ue_i].append(np.mean(mcs_data[ue_i]))
            else:
                ys[3][ue_i].append(0)
    else:
        print(f"W: {time.time()} :: STALE!")
    
    if len(xs[4]) == 0 or ts > xs[4][-1]:
        # Add x and y to lists
        xs[4].append(ts)
        while (ue_id > len(ys[4])):
            ys[4].append([])
        for ue_i in range(ue_id):
            ys[4][ue_i].append(retx_data[ue_i]-np.sum(ys[4][ue_i]))
    else:
        print(f"W: {time.time()} :: STALE!")

    if len(xs[5]) == 0 or ts > xs[5][-1]:
        # Add x and y to lists
        xs[5].append(ts)
        while (ue_id > len(ys[5])):
            ys[5].append([])
        for ue_i in range(ue_id):
            if (len(tpc_data[ue_i]) > 0):
                ys[5][ue_i].append(np.mean(tpc_data[ue_i]))
            else:
                ys[5][ue_i].append(0)
    else:
        print(f"W: {time.time()} :: STALE!")

    if len(xs[6]) == 0 or ts > xs[6][-1]:
        # Add x and y to lists
        xs[6].append(ts)
        while (ue_id > len(ys[6])):
            ys[6].append([])
        for ue_i in range(ue_id):
            if (len(mimo_data[ue_i]) > 0):
                ys[6][ue_i].append(np.mean(mimo_data[ue_i]))
            else:
                ys[6][ue_i].append(0)
    else:
        print(f"W: {time.time()} :: STALE!")

    end_time = time.time()
    print("Processing time: ", end_time - start_time)
    
    ax1.clear()
    ax1.set_xticklabels([])
    ax1.yaxis.set_major_formatter(FormatStrFormatter('%.2f'))
    ax1.set_ylabel("DL Tput\n (Mbit/s)")
    ax1.get_yaxis().set_label_coords(-0.05,0.5)
    ax1.set_xlim([xs[0][-1]-int(limit/INTERVAL), xs[0][-1]])

    # ax1.set_ylim([0, 1])
    print(ue_id)
    for ue_i in range(ue_id):
        # stacked_data = np.array(ys[0][ue_i])
        # for last_data in range(ue_i):
        #     stacked_data = stacked_data + np.array(ys[0][last_data])[len(ys[0][last_data]) - len(ys[0][ue_i]):]
        if (len(ys[0][ue_i]) > limit):
            if (np.sum(ys[0][ue_i][-limit:]) > 0):
                ax1.plot(xs[0][-limit:], ys[0][ue_i][-limit:], 
                        label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
        else:
            if (np.sum(ys[0][ue_i]) > 0):
                ax1.plot(xs[0][(len(xs[0]) - len(ys[0][ue_i])):], ys[0][ue_i], 
                        label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
    ax1.legend(loc='upper left', bbox_to_anchor=(1.0, 1.5), prop={'size': 23})

    ax2.clear()
    ax2.set_xticklabels([])
    ax2.yaxis.set_major_formatter(FormatStrFormatter('%.2f'))
    ax2.set_ylabel("UL Tput\n (Mbit/s)")
    for ue_i in range(ue_id):
        if (len(ys[1][ue_i]) > limit):
            if (np.sum(ys[1][ue_i][-limit:]) > 0):
                ax2.plot(xs[1][-limit:], ys[1][ue_i][-limit:], 
                        label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
        else:
            if (np.sum(ys[1][ue_i]) > 0):
                ax2.plot(xs[1][(len(xs[1]) - len(ys[1][ue_i])):], ys[1][ue_i], 
                    label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
    # ax2.legend(prop={'size': 15})
    ax2.get_yaxis().set_label_coords(-0.05,0.5)
    ax2.set_xlim([xs[0][-1]-int(limit/INTERVAL), xs[0][-1]])

    ax3.clear()
    ax3.set_xticklabels([])
    ax3.set_ylabel("PRB (%)")
    ax3.yaxis.set_major_formatter(FormatStrFormatter('%d'))
    ax3.set_xlim([xs[0][-1]-int(limit/INTERVAL), xs[0][-1]])
    ax3.get_yaxis().set_label_coords(-0.05,0.5)
    if (len(ys[2][0]) > limit):
        if (np.sum(ys[2][0][-limit:]) > 0):
            ax3.bar(xs[2][-limit:], ys[2][0][-limit:], label="UE {}".format(0), width=INTERVAL/1000, color=color_layers[np.mod(0, 9)])
    else:
        if (np.sum(ys[2][0]) > 0):
            ax3.bar(xs[2][(len(xs[2]) - len(ys[2][0])):], ys[2][0], label="UE {}".format(0), width=INTERVAL/1000, color=color_layers[np.mod(0, 9)])
    # for ue_i in range(1, ue_id):
    #     ax.bar(xs[(len(xs) - len(ys[ue_i])):], ys[ue_i], 
    #            bottom=ys[ue_i-1][(len(ys[ue_i-1]) - len(ys[ue_i])):], 
    #            label="UE {}".format(ue_i))
        
    for ue_i in range(1, ue_id):
        if (len(ys[2][ue_i]) > limit):
            if (np.sum(ys[2][0][-limit:]) > 0):
                bottoms = np.zeros(limit)
                for previous_ue in range(1, ue_i):
                    bottoms = bottoms + np.array(ys[2][previous_ue][-limit:])
                ax3.bar(xs[2][-limit:], ys[2][ue_i][-limit:], 
                    bottom=bottoms, width=INTERVAL/1000, 
                    label="UE {}".format(ue_i), color=color_layers[np.mod(ue_i, 9)])
        else:
            if (np.sum(ys[2][0]) > 0):
                bottoms = np.zeros(len(ys[2][ue_i]))
                for previous_ue in range(1, ue_i):
                    bottoms = bottoms + np.array(ys[2][previous_ue][(len(ys[2][previous_ue]) - len(ys[2][ue_i])):])
                ax3.bar(xs[2][(len(xs[2]) - len(ys[2][ue_i])):], ys[2][ue_i], 
                    bottom=bottoms, width=INTERVAL/1000, 
                    label="UE {}".format(ue_i), color=color_layers[np.mod(ue_i, 9)])
    # ax3.legend(prop={'size': 15})

    ax4.clear()
    ax4.set_xticklabels([])
    ax4.set_ylabel("MCS")
    ax4.get_yaxis().set_label_coords(-0.05,0.5)
    ax4.yaxis.set_major_formatter(FormatStrFormatter('%d'))
    # ax.set_ylim([0, 1])
    for ue_i in range(ue_id):
        if (len(ys[3][ue_i]) > limit):
            if (np.sum(ys[3][0][-limit:]) > 0):
                ax4.plot(xs[3][-limit:], ys[3][ue_i][-limit:], 
                    label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
        else:
            if (np.sum(ys[3][0]) > 0):
                ax4.plot(xs[3][(len(xs[3]) - len(ys[3][ue_i])):], ys[3][ue_i], 
                    label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
    # ax4.legend(prop={'size': 15})
    ax4.set_xlim([xs[0][-1]-int(limit/INTERVAL), xs[0][-1]])

    ax5.clear()
    ax5.set_xticklabels([])
    ax5.set_ylabel("ReTXs")
    ax5.get_yaxis().set_label_coords(-0.05,0.5)
    ax5.yaxis.set_major_formatter(FormatStrFormatter('%d'))
    xs4 = xs[4]
    ys4 = ys[4]
    if(len(ys4[0]) > limit):
        if (np.sum(ys4[0][-limit:]) > 0):
            ax5.bar(xs4[-limit:], ys4[0][-limit:], label="UE {}".format(0), width=INTERVAL/1000, color=color_layers[np.mod(0, 9)])
    else: 
        if (np.sum(ys4[0]) > 0):
            ax5.bar(xs4[(len(xs4) - len(ys4[0])):], ys4[0], label="UE {}".format(0), width=INTERVAL/1000, color=color_layers[np.mod(0, 9)])
    for ue_i in range(1, ue_id):
        if (len(ys4[ue_i]) > limit):
            if (np.sum(ys4[ue_i][-limit:]) > 0):
                bottoms = np.zeros(limit)
                for previous_ue in range(1, ue_i):
                    bottoms = bottoms + np.array(ys4[previous_ue][-limit:])
                ax5.bar(xs4[-limit:], ys4[ue_i][-limit:], 
                    bottom=bottoms, width=INTERVAL/1000,
                    label="UE {}".format(ue_i), color=color_layers[np.mod(ue_i, 9)])
        else:
            if (np.sum(ys4[ue_i]) > 0):
                bottoms = np.zeros(len(ys4[ue_i]))
                for previous_ue in range(1, ue_i):
                    bottoms = bottoms + np.array(ys4[previous_ue][(len(ys4[previous_ue]) - len(ys4[ue_i])):])
                ax5.bar(xs4[(len(xs4) - len(ys4[ue_i])):], ys4[ue_i], 
                    bottom=bottoms, width=INTERVAL/1000,
                    label="UE {}".format(ue_i), color=color_layers[np.mod(ue_i, 9)])
        # ax4.plot(xs[4][(len(xs[4]) - len(ys[4][ue_i])):], ys[4][ue_i], 
        #         label="UE {}".format(ue_i))
    # ax5.legend(prop={'size': 15})
    ax5.set_xlim([xs[0][-1]-int(limit/INTERVAL), xs[0][-1]])

    ax6.clear()
    ax6.set_xticklabels([])
    ax6.set_ylabel("TPC")
    ax6.get_yaxis().set_label_coords(-0.05,0.5)
    ax6.yaxis.set_major_formatter(FormatStrFormatter('%.1f'))
    # ax.set_ylim([0, 1])
    for ue_i in range(ue_id):
        if (len(ys[5][ue_i]) > limit):
            if (np.sum(ys[5][ue_i][-limit:]) > 0):
                ax6.plot(xs[5][-limit:], ys[5][ue_i][-limit:], 
                    label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
        else:
            if (np.sum(ys[5][ue_i]) > 0):
                ax6.plot(xs[5][(len(xs[5]) - len(ys[5][ue_i])):], ys[5][ue_i], 
                    label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
    # ax6.legend(prop={'size': 15})
    ax6.set_xlim([xs[0][-1]-int(limit/INTERVAL), xs[0][-1]])

    ax7.clear()
    ax7.set_xlabel("Time (s)")
    ax7.set_ylabel("MIMO\n Layers")
    ax7.get_yaxis().set_label_coords(-0.05,0.5)
    ax7.yaxis.set_major_formatter(FormatStrFormatter('%.1f'))
    # ax.set_ylim([0, 1])
    for ue_i in range(ue_id):
        if (len(ys[6][ue_i]) > limit):
            if (np.sum(ys[5][ue_i][-limit:]) > 0):
                ax7.plot(xs[6][-limit:], ys[6][ue_i][-limit:], 
                    label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
        else:
            if (np.sum(ys[5][ue_i]) > 0):
                ax7.plot(xs[6][(len(xs[6]) - len(ys[6][ue_i])):], ys[6][ue_i], 
                    label="UE {}".format(ue_i), linewidth=2.5, color=color_layers[np.mod(ue_i, 9)])
    # ax7.legend(prop={'size': 15})
    ax7.set_xlim([xs[0][-1]-int(limit/INTERVAL), xs[0][-1]])
    
# save video (only to attach here) 
#anim = mpl.animation.FuncAnimation(fig, animate, fargs=([time.time()], [None]), interval=1, frames=3 * PLOT_LIMIT, repeat=False)
#anim.save(ANIM_FILENAME, writer='imagemagick', fps=10)
#print(f"I: Saved to `{ANIM_FILENAME}`")

# show interactively
anim = mpl.animation.FuncAnimation(fig, animate, 
    fargs=([[], [], [], [], [], [], []], [[[]], [[]], [[]], [[]], [[]], [[]], [[]]]), interval=INTERVAL)
# anim.save(ANIM_FILENAME, writer='imagemagick', fps=20)
#print(f"I: Saved to `{ANIM_FILENAME}`")

plt.show()
plt.close()