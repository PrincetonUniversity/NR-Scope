import tkinter as tk
import time
import asyncio
import threading
import random
import queue
import csv
import re
import argparse
import time

parser = argparse.ArgumentParser()

parser.add_argument("-i", "--interval", type=float, help="Animation interval in ms.")
args = parser.parse_args()

TIME_INTERVAL = args.interval

# crnti => (last-seen timestamp, label, frame, occupied, MIMO_layer)
labels = {}
seen_ues = []

lock = threading.Lock()
tk_internal_lock = threading.Lock()

color_layers = [
    ["#f55702", "#f78040", "#f7a97e", "#f5c7ae", "#f5f3f2"],
    ["#ff00d4", "#fa4bdc", "#fa84e6", "#fab6ee", "#f5f3f2"],
    ["#9200fa", "#ab40f7", "#cc88fc", "#ddb4fa", "#f5f3f2"],
    ["#1500ff", "#4f40f7", "#897ffa", "#bcb6fa", "#f5f3f2"],
    ["#ff0000", "#fa4141", "#fc8686", "#fab6b6", "#f5f3f2"],
    ["#f2e200", "#faee41", "#f5ed84", "#faf6be", "#f5f3f2"],
    ["#6aff00", "#97fa50", "#bcfc8d", "#ddffc4", "#f5f3f2"],
    ["#05499c", "#2d5f9c", "#4f739e", "#75889e", "#f5f3f2"],
    ["#b56d00", "#ba8534", "#ba9863", "#baa98d", "#f5f3f2"]] # 9 elements

'''
NR-Scope fetcher
'''
CSV_LINE_IDX = 0

class AsyncioThread(threading.Thread):
    def __init__(self, the_queue, max_data):
        self.asyncio_loop = asyncio.get_event_loop()
        self.the_queue = the_queue
        self.max_data = max_data
        threading.Thread.__init__(self)

    def run(self):
        # self.asyncio_loop.run_until_complete(self.do_data())
        # print("done123!")

        # simulate to-be-removed
        lines_to_fetch_before_t = None
        while True:
            print("new round of CSV reading")
            with open("t_mobile2_ueactivity_afternoon_3.csv", newline='') as csvfile:
                with lock:
                    reader = csv.DictReader(csvfile)
                    for row in reader:
                        if row['rnti'].isnumeric():
                            if lines_to_fetch_before_t is None:
                                lines_to_fetch_before_t = float(row['timestamp']) + TIME_INTERVAL

                            if float(row['timestamp']) <= lines_to_fetch_before_t:
                                self.the_queue.put((int(row['rnti']), int(row['transport_block_size']), int(row['nof_layers'])))
                            else:
                                break
            
            lines_to_fetch_before_t += TIME_INTERVAL
            time.sleep(TIME_INTERVAL)

    # async def do_data(self):
    #     """ Creating and starting 'maxData' asyncio-tasks. """
    #     tasks = [
    #         self.create_dummy_data(key)
    #         for key in range(self.max_data)
    #     ]
    #     await asyncio.wait(tasks)

    # async def create_dummy_data(self, key):
    #     """ Create data and store it in the queue. """
    #     sec = random.randint(1, 15)
    #     data = '{}'.format(random.randint(1, 65519))
    #     await asyncio.sleep(sec)

    #     self.the_queue.put((key, data))

'''
 GUI main thread
'''
RNTI = 3245

fetcher_thread = None
shared_queue = queue.Queue()


window = tk.Tk()

def decode_riv(riv, scs, point_a):
    bwp_size = 275
    tmp_rb_len = riv // bwp_size
    tmp = riv // bwp_size + riv % bwp_size
    rb_len = None
    rb_start = None
    if tmp < bwp_size:
        rb_len = tmp_rb_len + 1
        rb_start = riv % bwp_size
    else:
        rb_len = bwp_size - tmp_rb_len + 1
        rb_start = bwp_size - 1 - (riv % bwp_size)
    
    start = point_a + (rb_start * 12 * int(re.findall(r'\d+', scs)[0]) / 1000)
    len = rb_len * 12 * int(re.findall(r'\d+', scs)[0]) / 1000
    return (start, len)


def search_meta_info(label):
    ssb_freq = None
    scs = None
    duplix = None
    pci = None
    band = None
    carrier_bw = None
    pointA = None
    bwp_used = None
    bwp0_dl_lbw = None
    bwp0_ul_lbw = None
    bwp1_dl_lbw = None
    bwp1_ul_lbw = None
    with open('stdout_moso.txt', 'r') as file:
        for line in file:
            if "c-freq=" in line:
                tokens = line.split(" ")
                for token in tokens:
                    if "c-freq=" in token:
                        ssb_freq = token.split("=")[1].replace(";", "").replace("\n", "")
                        print("ssb_freq")

                    if "scs=" in token:
                        scs = token.split("=")[1].replace(";", "").replace("\n", "")
                        print("scs")
                    if "duplex=" in token:
                        duplex = token.split("=")[1].replace(";", "").replace("\n", "")
                        print("duplex")
            if "N_id: " in line:
                pci = line.split(" ")[1].replace("\n", "")
                print("pci")
            
            if "freqBandIndicatorNR" in line:
                band = line.split(": ")[1].replace("\n", "")
                print("band")
            
            if "carrierBandwidth" in line:
                carrier_bw = int(line.split(": ")[1].replace("\n", ""))
                # convert from PRB # to Hz
                carrier_bw = carrier_bw * 12 * int(re.findall(r'\d+', scs)[0]) / 1000
                print("carrier_bw")
            
            if "pointA" in line:
                pointA = float(line.split(": ")[1].replace("\n", ""))/1000000
                print("pointA")
            
            if "bwp0 used" in line:
                bwp_used = 0
            
            if "bwp1 used" in line:
                bwp_used = 1
            
            if "initial_dl_bwp_lbw and initial_ul_bwp_lbw: " in line:
                bwp0_dl_lbw = int(line.split(": ")[1].split("; ")[0])
                bwp0_ul_lbw = int(line.split(": ")[1].split("; ")[1].replace("\n", ""))
            
            
    
    if ssb_freq is None or scs is None or duplex is None or pci is None:
        label.after(1000, search_meta_info, label)
    else:
        dl_start, dl_len = decode_riv(bwp0_dl_lbw if bwp_used == 0 else bwp1_dl_lbw, scs, pointA)
        ul_start, ul_len = decode_riv(bwp0_ul_lbw if bwp_used == 0 else bwp1_ul_lbw, scs, pointA)
        cell_info = f"""
            CELL INFO
            PCI (cell id): {pci}
            SSB (5G becon) freq: {ssb_freq}MHz
            duplix mode: {duplex}
            OFDM subcarrier spacing: {scs}
            band ID: {band}
            channel bandwidth: {carrier_bw}MHz
            point A (channel lower bound): {pointA}MHz
            BWP (bandwidth part) id used: {bwp_used}
            DL BWP start and length: {dl_start}MHz and {dl_len}MHz
            UL BWP start and length: {ul_start}MHz and {ul_len}MHz
        """
        label.config(text=cell_info, font=("Arial", 20))

def decrease_freshness(rnti_and_label):
    with lock:
        rnti, label = rnti_and_label
        if labels[rnti][3]:
            current_time = time.time()
            t_diff = int(current_time - labels[rnti][0])
            if t_diff <= 4:
                rnti_idx = seen_ues.index(rnti)
                label.config(bg=color_layers[rnti_idx % 9][t_diff])
                label.after(300, decrease_freshness, (rnti, label))
            else:
                # this RNTI expired; remove the frame for others
                print("Expired; freed")
                labels[rnti][3] = False
                label.config(text=f"")
                label.config(bg=None)

def refresh_data(rt_window):
    global RNTI, CSV_LINE_IDX
    if not fetcher_thread.is_alive() and shared_queue.empty():
        return

    # refresh the GUI with new data from the queue
    consume_idx = 0
    with lock:
        while not shared_queue.empty():
            data, tbs, mimo_layer = shared_queue.get()
            if consume_idx < CSV_LINE_IDX:
                consume_idx += 1
                continue
            consume_idx += 1
            if tbs == 0:
                print("tbs 0; skip")
                continue
            print(f"data: {data}")
            data_idx = None
            if data not in seen_ues:
                seen_ues.append(data)
            
            data_idx = seen_ues.index(data)

            # update the labels dictionary
            
            # case 1: this RNTI is active, update its timestamp
            if data in labels and labels[data][3] == True:
                labels[data][0] = time.time()
                labels[data][1].config(text=f"RNTI: {data}\nMIMO: {mimo_layer}")

            else:
                # case 2: not exists, find an empty slot and put it there
                empty_spot = False
                key_to_del = None
                copied_val = None
                for key, value in labels.items():
                    if value[3] == False:
                        empty_spot = True
                        copied_val = value
                        key_to_del = key
                        break
                
                if (empty_spot):
                    del labels[key_to_del]
                    labels[data] = copied_val
                    labels[data][3] = True
                    labels[data][0] = time.time()
                    label = labels[data][1]
                    label.config(text=f"RNTI: {data}\nMIMO: {mimo_layer}")
                    label.config(bg=color_layers[data_idx % 9][0])
                    label.after(300, decrease_freshness, (data, label))
                # case 3: not exists, no existing empty slot; need to expand window (TODO)
                else:
                    print("OVERFLOW! Resizing...")

                    # for i in range(3, 6):
                    #     # window.columnconfigure(i, weight=1, minsize=75)
                    #     window.rowconfigure(i, weight=1, minsize=50)

                    #     for j in range(3):
                    #         frame = tk.Frame(
                    #             master=window,
                    #             relief=tk.RAISED,
                    #             borderwidth=1
                    #         )
                    #         frame.grid(row=i, column=j, padx=5, pady=5)

                    #         label = tk.Label(master=frame, text=f"", bg="#f5f3f2", width=10, height=5)
                            
                    #         labels[RNTI] = [time.time(), label, frame, False]
                    #         # label.after(1000, decrease_freshness, (RNTI, label))
                    #         label.pack(padx=5, pady=5)
                    #         RNTI += 1

    print('refresh data...')

    CSV_LINE_IDX = consume_idx

    rt_window.after(int(TIME_INTERVAL*1000), refresh_data, rt_window)




cell_info_loading = "NR-SCOPE SEARCHING CELL... LOADING CELL INFO..."

frame1 = tk.Frame(master=window, height=100)
frame1.grid(row=0, column=0, sticky='ew', columnspan=5)
meta_info = tk.Label(master=frame1, text=cell_info_loading, height=13, font=("Arial", 20))
meta_info.pack(padx=5, pady=5)
meta_info.after(1000, search_meta_info, meta_info)

for i in range(1, 7):
    window.rowconfigure(i, weight=1, minsize=50)
    for j in range(0, 5):
        window.columnconfigure(j, weight=1, minsize=75)
        frame = tk.Frame(
            master=window,
            relief=tk.RAISED,
            borderwidth=1
        )
        frame.grid(row=i, column=j, padx=5, pady=5)

        label = tk.Label(master=frame, text=f"", bg="#f5f3f2", width=10, height=5)
        
        labels[RNTI] = [time.time(), label, frame, False]
        # label.after(1000, decrease_freshness, (RNTI, label))
        label.pack(padx=5, pady=5)
        RNTI += 1

# create Thread object
fetcher_thread = AsyncioThread(shared_queue, 50)
window.after(int(TIME_INTERVAL*1000), refresh_data, window)
fetcher_thread.start()

window.title("UE tracker")
window.mainloop()
