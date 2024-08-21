import matplotlib.pyplot as plt
import numpy as np

# f1 = open("nontwin_time_data/time_series4.txt", "r")
f1 = open("twinrx_time_data/time_series_pre_resample_ifnores.txt", "r")



r = 23040000/33333333; 
fig, axs = plt.subplots(2, constrained_layout=True, sharex=True)
line_count = 0
for line in f1:
    line_count += 1
    # continue
    if line_count != 78:
        continue
    
    reformatted = line.replace(" ", "").replace("i", "j")
    complex_strs = reformatted.split(",")[:-1]

    cfs = []
    reals = []
    imgs = []

    for complex_str in complex_strs:
        cf = complex(complex_str)
        cfs.append(cf)
        reals.append(cf.real)
        imgs.append(cf.imag)
    
    x1s = np.arange(0, len(cfs), 1)
    y1s_real = np.array(reals)
    y1s_img = np.array(imgs)

    axs[0].plot(x1s, y1s_real, label='real')
    axs[1].plot(x1s, y1s_img, label='imag')

    print(f'sampled point num: {len(cfs)}')

# print(f'line_count: {line_count}')
# exit()

axs[0].title.set_text('real part')
axs[1].title.set_text('imag part')

axs[0].set_xlabel('time')
axs[1].set_xlabel('time')

axs[0].legend(loc="upper right")
axs[1].legend(loc="upper right")

plt.show()
f1.close()

