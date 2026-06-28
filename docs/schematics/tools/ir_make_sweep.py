#!/usr/bin/env python3
"""Genera un barrido seno logarítmico (Farina) para capturar IRs de cab por
deconvolución. Pásalo por el cab SOLO (amp en bypass) y guarda la salida; luego
ir_deconv.py usa ESTE mismo archivo como referencia para recuperar el IR.

Params fijos (deben coincidir con la deconvolución):
  SR=48000, F1=20 Hz, F2=22000 Hz, sweep=10 s, lead=0.1 s, tail=1.5 s, peak=0.5
Salida: mono 24-bit PCM WAV.
"""
import numpy as np, wave, sys

SR=48000; F1=20.0; F2=22000.0; T=10.0; LEAD=0.1; TAIL=1.5; PEAK=0.5
def main(out):
    t=np.arange(int(T*SR))/SR
    L=T/np.log(F2/F1)
    sweep=np.sin(2*np.pi*F1*L*(np.exp(t/L)-1.0))   # exponential (log) sweep
    fi=int(0.02*SR); fo=int(0.05*SR)               # de-click fades
    sweep[:fi]*=np.linspace(0,1,fi); sweep[-fo:]*=np.linspace(1,0,fo)
    sig=np.concatenate([np.zeros(int(LEAD*SR)), sweep*PEAK, np.zeros(int(TAIL*SR))])
    pcm=np.clip(np.round(sig*8388607.0),-8388608,8388607).astype(np.int32)
    b=bytearray()
    for v in pcm: b+= int(v).to_bytes(3,'little',signed=True)
    w=wave.open(out,'wb'); w.setnchannels(1); w.setsampwidth(3); w.setframerate(SR)
    w.writeframes(bytes(b)); w.close()
    print("wrote %s  %.2fs  %d samples @%dHz 24-bit mono"%(out,len(sig)/SR,len(sig),SR))
if __name__=="__main__":
    main(sys.argv[1] if len(sys.argv)>1 else "ir_sweep.wav")
