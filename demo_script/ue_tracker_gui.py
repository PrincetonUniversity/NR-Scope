import tkinter as tk
import time
import asyncio
import threading
import random
import queue
import csv


# crnti => (last-seen timestamp, label, frame, occupied, color_index)
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
            with open("a.csv", newline='') as csvfile:
                with lock:
                    reader = csv.DictReader(csvfile)
                    for row in reader:
                        if row['rnti'].isnumeric():
                            if lines_to_fetch_before_t is None:
                                lines_to_fetch_before_t = float(row['timestamp']) + 1

                            if float(row['timestamp']) <= lines_to_fetch_before_t:
                                self.the_queue.put(int(row['rnti']))
                            else:
                                break
            
            lines_to_fetch_before_t += 1
            time.sleep(1)

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

def search_meta_info(label):
    ssb_freq = None
    scs = None
    duplix = None
    pci = None
    with open('stdout.txt', 'r') as file:
        for line in file:
            if "c-freq=" in line:
                tokens = line.split(" ")
                for token in tokens:
                    if "c-freq=" in token:
                        ssb_freq = token.split("=")[1].replace(";", "")
                        print("ssb_freq")

                    if "scs=" in token:
                        scs = token.split("=")[1].replace(";", "")
                        print("scs")
                    if "duplex=" in token:
                        duplex = token.split("=")[1].replace(";", "")
                        print("duplex")
            if "N_id: " in line:
                pci = line.split(" ")[1]
                print("pci")
    
    if ssb_freq is None or scs is None or duplex is None or pci is None:
        label.after(1000, search_meta_info, label)
    else:
        cell_info = f"""
            CELL INFO
            PCI (cell id): {pci}
            SSB (5G becon) freq: {ssb_freq}
            duplix mode: {duplix}
            OFDM subcarrier spacing: {scs}
        """
        label.config(text=cell_info)

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
                label.config(text=f"free")
                label.config(bg=None)

def refresh_data(rt_window):
    global RNTI, CSV_LINE_IDX
    if not fetcher_thread.is_alive() and shared_queue.empty():
        return

    # refresh the GUI with new data from the queue
    consume_idx = 0
    with lock:
        while not shared_queue.empty():
            data = shared_queue.get()
            if consume_idx < CSV_LINE_IDX:
                consume_idx += 1
                continue
            consume_idx += 1
            print(f"data: {data}")
            data_idx = None
            if data not in seen_ues:
                seen_ues.append(data)
            
            data_idx = seen_ues.index(data)

            # update the labels dictionary
            
            # case 1: this RNTI is active, update its timestamp
            if data in labels and labels[data][3] == True:
                labels[data][0] = time.time()

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
                    label.config(text=f"RNTI: {data}")
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

    rt_window.after(1000, refresh_data, rt_window)




cell_info_loading = "NR-SCOPE SEARCHING CELL... LOADING CELL INFO..."

frame1 = tk.Frame(master=window, height=100)
frame1.grid(row=0, column=0, sticky='ew', columnspan=5)
meta_info = tk.Label(master=frame1, text=cell_info_loading, height=10)
meta_info.pack(padx=5, pady=5)
meta_info.after(1000, search_meta_info, meta_info)

for i in range(1, 11):
    window.rowconfigure(i, weight=1, minsize=50)
    for j in range(0, 5):
        window.columnconfigure(j, weight=1, minsize=75)
        frame = tk.Frame(
            master=window,
            relief=tk.RAISED,
            borderwidth=1
        )
        frame.grid(row=i, column=j, padx=5, pady=5)

        label = tk.Label(master=frame, text=f"free", bg="#f5f3f2", width=10, height=5)
        
        labels[RNTI] = [time.time(), label, frame, False]
        # label.after(1000, decrease_freshness, (RNTI, label))
        label.pack(padx=5, pady=5)
        RNTI += 1

# create Thread object
fetcher_thread = AsyncioThread(shared_queue, 50)
window.after(500, refresh_data, window)
fetcher_thread.start()

window.title("UE tracker")
window.mainloop()
