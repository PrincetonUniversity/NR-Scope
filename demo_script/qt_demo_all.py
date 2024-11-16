import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore
import numpy as np
import sys
import argparse
import time

parser = argparse.ArgumentParser()

parser.add_argument("-i", "--interval", type=int, help="Animation interval in ms.")
args = parser.parse_args()

BUFFER_LEN = 64
DATA_FILENAME = "t_mobile2_ueactivity_afternoon_3.csv"
ANIM_FILENAME = "video.gif"

INTERVAL = args.interval #ms
PLOT_LIMIT = int(10 * 1000 / INTERVAL) # 5 seconds

SLOT_TIME = 1
DL_TOTAL = 0.8
PRB_NUM = INTERVAL / SLOT_TIME * DL_TOTAL * 51 * 12

FONT_SIZE = 45

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

file_cur = 1
first_time = 0
ue_list = dict()
ue_id = 0
i = 0
data = []
ndi_data = [[[] for harq_id in range(16)] for ue_i in range(len(ue_list))]
xs = [[], [], [], [], [], [], []]
ys = [[[]], [[]], [[]], [[]], [[]], [[]], [[]]]

# Initialize the Qt application
app = QtWidgets.QApplication([])

# Create a layout widget
win = pg.GraphicsLayoutWidget(show=True, title="Dynamic Number of Lines in Each Subplot")
win.resize(1000, 600)
win.setWindowTitle("Dynamic Lines Example")

# Variables for managing subplots and their lines
num_subplots = 7  # Fixed number of subplots
subplots = []     # Store subplots
all_lines = []    # Store lines for each subplot

# Function to initialize subplots
def initialize_subplots():
    global subplots, all_lines
    subplots.clear()
    all_lines.clear()
    
    # Create subplots and initialize line holders
    for i in range(num_subplots):
        plot = win.addPlot(row=i, col=0)
        subplots.append(plot)
        all_lines.append([])  # Empty line list for each subplot
        if (i < 6):
            plot.getAxis('bottom').setTicks([])  # Remove tick labels on the x-axis
        if (i == 0):
            plot.setLabel('left', 'DL Tput\n(Mibits/s)')
        if (i == 1):
            plot.setLabel('left', 'UL Tput\n(Mibits/s)')
        if (i == 2):
            plot.setLabel('left', 'PRB (%)')
        if (i == 3):
            plot.setLabel('left', 'MCS')
        if (i == 4):
            plot.setLabel('left', 'ReTxs')
        if (i == 5):
            plot.setLabel('left', 'TPC')
        if (i == 6):
            plot.setLabel('left', 'MIMO\nLayers')
            plot.setLabel('bottom', 'Time (s)')
        

# Initialize subplots at the beginning
initialize_subplots()

# Function to dynamically set the number of lines in each subplot
def set_num_lines(subplot_index, num_lines):
    global all_lines
    
    # Get the subplot and clear current lines if needed
    subplot = subplots[subplot_index]
    subplot.clear()
    all_lines[subplot_index] = []  # Reset the lines list for this subplot

    # Create new lines for the subplot
    if (subplot_index == 0):
        for j in range(num_lines):
          curve = subplot.plot(pen=pg.mkPen(color_layers[np.mod(j, 9)], width=5), name='UE {}'.format(j))  # Different color for each line
          all_lines[subplot_index].append(curve)
    else:
      for j in range(num_lines):
          curve = subplot.plot(pen=pg.mkPen(color_layers[np.mod(j, 9)], width=5))  # Different color for each line
          all_lines[subplot_index].append(curve)
    

# Function to update data in each line within each subplot
# def update():
#     x = np.linspace(0, 10, 100)
#     for i, lines in enumerate(all_lines):
#         for j, line in enumerate(lines):
#             # Generate data for each line with different phase shifts
#             y = np.sin(x + j * np.pi / 5 + update.counter / 10 + i * np.pi / 3)
#             line.setData(x, y)
#     update.counter += 1

# # Initialize a counter for the update function
# update.counter = 0

# # Set up a timer to periodically call the update function
# timer = QtCore.QTimer()
# timer.timeout.connect(update)
# timer.start(50)  # Update every 50 ms

# Example: Change the number of lines dynamically within each subplot
def change_line_count_in_subplots(nums):
    set_num_lines(0, 2)  # Set 2 lines in subplot 1
    set_num_lines(1, 4)  # Set 4 lines in subplot 2
    set_num_lines(2, 3)  # Set 3 lines in subplot 3

def get_data(filename, delay=0.0):
    print("getting data")
    with open(filename, "r") as f:
        data = f.readlines()
        if delay:
            time.sleep(delay)
    return data

# Function to update the plots with new data
def update():
    global i, xs, ys, file_cur, first_time, ue_list, ue_id, data
    i = i + 1
    # grab the data
    # try:
    start_time = time.time()
    if (len(data) == 0):
        data = get_data(DATA_FILENAME)
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
    if (len(all_lines[0]) < ue_id):
      set_num_lines(0, ue_id)  # Set 2 lines in subplot 1
    for ue_i in range(ue_id):
      if (len(ys[0][ue_i]) > PLOT_LIMIT):
        all_lines[0][ue_i].setData(xs[0][-PLOT_LIMIT:], ys[0][ue_i][-PLOT_LIMIT:])
      else:
        all_lines[0][ue_i].setData(xs[0][(len(xs[0]) - len(ys[0][ue_i])):], ys[0][ue_i])

    if (len(all_lines[1]) < ue_id):
      set_num_lines(1, ue_id)  # Set 2 lines in subplot 1
    for ue_i in range(ue_id):
      if (len(ys[1][ue_i]) > PLOT_LIMIT):
        all_lines[1][ue_i].setData(xs[1][-PLOT_LIMIT:], ys[1][ue_i][-PLOT_LIMIT:])
      else:
        all_lines[1][ue_i].setData(xs[1][(len(xs[0]) - len(ys[1][ue_i])):], ys[1][ue_i])
    
    if (len(all_lines[2]) < ue_id):
      set_num_lines(2, ue_id)  # Set 2 lines in subplot 1
    for ue_i in range(ue_id):
      if (len(ys[2][ue_i]) > PLOT_LIMIT):
        all_lines[2][ue_i].setData(xs[2][-PLOT_LIMIT:], ys[2][ue_i][-PLOT_LIMIT:])
      else:
        all_lines[2][ue_i].setData(xs[2][(len(xs[0]) - len(ys[2][ue_i])):], ys[2][ue_i])

    if (len(all_lines[3]) < ue_id):
      set_num_lines(3, ue_id)  # Set 2 lines in subplot 1
    for ue_i in range(ue_id):
      if (len(ys[3][ue_i]) > PLOT_LIMIT):
        all_lines[3][ue_i].setData(xs[3][-PLOT_LIMIT:], ys[3][ue_i][-PLOT_LIMIT:])
      else:
        all_lines[3][ue_i].setData(xs[3][(len(xs[0]) - len(ys[3][ue_i])):], ys[1][ue_i])
    
    if (len(all_lines[4]) < ue_id):
      set_num_lines(4, ue_id)  # Set 2 lines in subplot 1
    for ue_i in range(ue_id):
      if (len(ys[4][ue_i]) > PLOT_LIMIT):
        all_lines[4][ue_i].setData(xs[4][-PLOT_LIMIT:], ys[4][ue_i][-PLOT_LIMIT:])
      else:
        all_lines[4][ue_i].setData(xs[4][(len(xs[0]) - len(ys[4][ue_i])):], ys[4][ue_i])

    if (len(all_lines[5]) < ue_id):
      set_num_lines(5, ue_id)  # Set 2 lines in subplot 1
    for ue_i in range(ue_id):
      if (len(ys[5][ue_i]) > PLOT_LIMIT):
        all_lines[5][ue_i].setData(xs[5][-PLOT_LIMIT:], ys[5][ue_i][-PLOT_LIMIT:])
      else:
        all_lines[5][ue_i].setData(xs[5][(len(xs[0]) - len(ys[5][ue_i])):], ys[5][ue_i])

    if (len(all_lines[6]) < ue_id):
      set_num_lines(6, ue_id)  # Set 2 lines in subplot 1
    for ue_i in range(ue_id):
      if (len(ys[6][ue_i]) > PLOT_LIMIT):
        all_lines[6][ue_i].setData(xs[6][-PLOT_LIMIT:], ys[6][ue_i][-PLOT_LIMIT:])
      else:
        all_lines[6][ue_i].setData(xs[6][(len(xs[0]) - len(ys[6][ue_i])):], ys[6][ue_i])

# Adjust line count after 3 seconds to demonstrate dynamic changes
# QtCore.QTimer.singleShot(3000, change_line_count_in_subplots)
timer = QtCore.QTimer()
timer.timeout.connect(update)
timer.start(INTERVAL)  # Update every 50 ms

# Start the Qt event loop
if __name__ == "__main__":
    sys.exit(app.exec_())
