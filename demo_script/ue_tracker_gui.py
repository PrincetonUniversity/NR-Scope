import tkinter as tk
import time
import asyncio
import threading
import random
import queue
import csv


# crnti => (last-seen timestamp, label, frame, occupied)
labels = {}

color_layers = ["#f55702", "#f78040", "#f7a97e", "#f5c7ae", "#f5f3f2"]

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
        lines_to_fetch = 20
        while True:
            print("new round of CSV reading")
            with open("/home/wanhr/Documents/codes/cpp/NG-Scope-5G/build/nrscope/src/a.csv", newline='') as csvfile:
                reader = csv.DictReader(csvfile)
                fetched_l_count = 0
                for row in reader:
                    if row['rnti'].isnumeric():
                        self.the_queue.put(int(row['rnti']))
                        fetched_l_count += 1
                        if fetched_l_count >= lines_to_fetch:
                            break
            
            lines_to_fetch += 20
            time.sleep(0.3)


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

def decrease_freshness(rnti_and_label):
    rnti, label = rnti_and_label
    if labels[rnti][3]:
        current_time = time.time()
        t_diff = int(current_time - labels[rnti][0])
        if t_diff <= 4:
            label.config(bg=color_layers[t_diff])
            label.after(1000, decrease_freshness, (rnti, label))
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
    while not shared_queue.empty():
        data = shared_queue.get()
        if consume_idx < CSV_LINE_IDX:
            consume_idx += 1
            continue
        consume_idx += 1
        print(f"data: {data}")

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
                label.config(bg="#f55702")
                label.after(1000, decrease_freshness, (data, label))
            # case 3: not exists, no existing empty slot; need to expand window (TODO)
            else:
                print("OVERFLOW! Resizing...")
                

                for i in range(3, 6):
                    # window.columnconfigure(i, weight=1, minsize=75)
                    window.rowconfigure(i, weight=1, minsize=50)

                    for j in range(3):
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

    print('refresh data...')

    CSV_LINE_IDX = consume_idx

    rt_window.after(1000, refresh_data, rt_window)

for i in range(3):
    window.columnconfigure(i, weight=1, minsize=75)
    window.rowconfigure(i, weight=1, minsize=50)

    for j in range(0, 3):
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
window.after(500, refresh_data, window)
fetcher_thread.start()

window.title("UE tracker")
window.mainloop()
