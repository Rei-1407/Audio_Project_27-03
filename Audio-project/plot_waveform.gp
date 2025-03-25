set terminal png size 1200,800
set output 'audio_waveform.png'
set title 'Audio Waveform'
set xlabel 'Sample Index'
set ylabel 'Amplitude'
set yrange [-1:1]
set grid
plot 'audio_waveform.dat' using 1:2 with lines title 'Left Channel' lc rgb '#0060ad' lw 1, \
     'audio_waveform.dat' using 1:3 with lines title 'Right Channel' lc rgb '#dd181f' lw 1
