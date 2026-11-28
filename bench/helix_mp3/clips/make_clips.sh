#!/bin/sh
# Generates the benchmark clips from a synthetic signal (no third-party audio, so no license issue).
# 8 s, 44.1 kHz stereo: chord progression with harmonics and vibrato, stereo-panned arpeggio,
# pink noise bed and decaying noise bursts (drum-like transients), which exercise the
# Huffman/side-info paths and block switching more than pure tones would.
# Needs ffmpeg and lame (LAME 3.100 was used for the committed files). -t: no Xing/Info frame, every frame is audio.
set -e
cd "$(dirname "$0")"
D=8
ffmpeg -v error -y -filter_complex "
aevalsrc='0.10*(sin(2*PI*(220*pow(2,floor(mod(t,4))*5/12))*t+0.3*sin(2*PI*5*t))+0.5*sin(4*PI*(220*pow(2,floor(mod(t,4))*5/12))*t)+0.25*sin(6*PI*(220*pow(2,floor(mod(t,4))*5/12))*t))
 +0.08*sin(2*PI*(440*pow(2,floor(mod(t*8,8))*2/12))*t)*(0.5+0.5*sin(2*PI*0.5*t))
 +0.06*sin(2*PI*110*t)*exp(-3*mod(t,0.5))
 |0.10*(sin(2*PI*(277.2*pow(2,floor(mod(t,4))*5/12))*t+0.3*sin(2*PI*6*t))+0.5*sin(4*PI*(277.2*pow(2,floor(mod(t,4))*5/12))*t)+0.25*sin(6*PI*(330*pow(2,floor(mod(t,4))*5/12))*t))
 +0.08*sin(2*PI*(440*pow(2,floor(mod(t*8,8))*2/12))*t)*(0.5-0.5*sin(2*PI*0.5*t))
 +0.06*sin(2*PI*110*t)*exp(-3*mod(t,0.5))':s=44100:d=$D[tone];
anoisesrc=c=pink:a=0.05:s=44100:d=$D:seed=1,pan=stereo|c0=c0|c1=c0[bed];
anoisesrc=c=white:a=0.6:s=44100:d=$D:seed=2,volume='exp(-25*mod(t,0.25))':eval=frame,highpass=f=2000,aformat=channel_layouts=mono,pan=stereo|c0=0.8*c0|c1=0.5*c0[hat];
anoisesrc=c=brown:a=0.9:s=44100:d=$D:seed=3,volume='exp(-12*mod(t,0.5))':eval=frame,lowpass=f=200,aformat=channel_layouts=mono,pan=stereo|c0=c0|c1=c0[kick];
[tone][bed][hat][kick]amix=inputs=4:normalize=0,alimiter=limit=0.9[out]" -map "[out]" -ar 44100 -ac 2 -c:a pcm_s16le source.wav
lame --quiet -b 128 --cbr -m j -q 2 -t source.wav clip_128k.mp3
lame --quiet -b 320 --cbr -m j -q 2 -t source.wav clip_320k.mp3
rm -f source.wav
ls -l clip_*.mp3
