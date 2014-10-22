import re
import matplotlib.pyplot as plt

samples       = []
fft           = []
fft_ticks     = None

# read samples and fft from text file
with open('teraterm.log') as f:
    for line in f:
        
        # look for samples
        m = re.search('sample ([0-9]+)\s*:\s*(-?[0-9]+)', line)
        if m:
            samples    += [int(m.group(2))]
        
        # look for fft
        m = re.search('fft ([0-9]+)\s*:\s*([0-9]+)', line)
        if m:
            fft        += [int(m.group(2))]
        
        # look for fft_ticks
        m = re.search('FFT done in ([0-9]+) 32kHz ticks', line)
        if m:
            fft_ticks   = int(m.group(1))

# find FFT peak frequency
peak_freq = fft.index(max(fft[1:int(len(fft)/2)]))

# plot histogram
plt.figure(1)
plt.plot(samples)
plt.title('raw data ({0} samples)'.format(len(samples)))

plt.figure(2)
plt.plot(fft)
plt.title(
    'fft (calculated in {0}ms, peak at {1}Hz)'.format(
        int(1000.0*float(fft_ticks)/32768.0),
        peak_freq
    )
)

plt.show()