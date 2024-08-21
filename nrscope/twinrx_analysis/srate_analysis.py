'''
    Given 200MHz clock rate, we wannna a rx srate x that divides 200MHz, yet 30,000Hz also divides x.

    Also x should be large enough to include the whole channel. E.g., at least 20 * 12 * 30,000Hz, the SSB bw
'''

i = 1

CLOCK_RATE = 184320000
SSB_SCS = 15000
SSB_BW = 20 * 12 * SSB_SCS

while True:
    candidate_srate = i * SSB_SCS

    if candidate_srate > CLOCK_RATE:
        break

    if CLOCK_RATE % candidate_srate == 0 and candidate_srate >= SSB_BW:
        print(f"{candidate_srate} is a valid srate")
    
    i += 1

'''
for 184.32MHz:
3840000 is a valid srate
5760000 is a valid srate
7680000 is a valid srate
11520000 is a valid srate
15360000 is a valid srate
23040000 is a valid srate
30720000 is a valid srate
46080000 is a valid srate
61440000 is a valid srate
92160000 is a valid srate
184320000 is a valid srate
'''

'''
for 200MHz:

None :(
'''