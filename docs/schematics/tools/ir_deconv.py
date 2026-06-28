#!/usr/bin/env python3
"""Recupera el IR de un cab por deconvolución del barrido (ir_make_sweep.py).
Uso: ir_deconv.py <recording.wav> <out_ir.wav> [sweep.wav] [ir_ms]
  recording = el barrido pasado por el CAB SOLO (amp en bypass), 48 kHz.
  sweep     = el ir_sweep.wav original (default busca junto al recording).
Salida: IR mono 48 kHz float32, recortado/ventaneado/normalizado (formato engine).
Regularized freq-domain: H = Y·conj(X)/(|X|²+eps); luego ventana alrededor del pico.
"""
import sys, struct, numpy as np

def readwav(p):
    b=open(p,'rb').read(); assert b[:4]==b'RIFF' and b[8:12]==b'WAVE', "not WAV: "+p
    pos=12; fmt=None; data=None
    while pos+8<=len(b):
        cid=b[pos:pos+4]; sz=struct.unpack('<I',b[pos+4:pos+8])[0]; ch=b[pos+8:pos+8+sz]
        if cid==b'fmt ': fmt=struct.unpack('<HHIIHH',ch[:16])
        elif cid==b'data': data=ch
        pos+=8+sz+(sz&1)
    af,nch,sr,_,_,bps=fmt
    if   af==3 and bps==32: a=np.frombuffer(data,'<f4').astype(np.float64)
    elif af==1 and bps==16: a=np.frombuffer(data,'<i2').astype(np.float64)/32768
    elif af==1 and bps==24:
        x=np.frombuffer(data,np.uint8).reshape(-1,3).astype(np.int32); a=((x[:,0]|(x[:,1]<<8)|(x[:,2]<<16))<<8>>8)/8388608.0
    elif af==1 and bps==32: a=np.frombuffer(data,'<i4').astype(np.float64)/2147483648
    else: raise SystemExit("unsupported fmt %d/%d"%(af,bps))
    if nch>1: a=a.reshape(-1,nch).mean(1)
    return a, sr

def write_f32(p, x, sr=48000):
    x=x.astype('<f4'); ds=len(x)*4
    h=b'RIFF'+struct.pack('<I',36+ds)+b'WAVE'+b'fmt '+struct.pack('<IHHIIHH',16,3,1,sr,sr*4,4,32)+b'data'+struct.pack('<I',ds)
    open(p,'wb').write(h+x.tobytes())

def main():
    rec_p=sys.argv[1]; out_p=sys.argv[2]
    sweep_p=sys.argv[3] if len(sys.argv)>3 else __import__('os').path.join(__import__('os').path.dirname(rec_p) or '.', 'ir_sweep.wav')
    ir_ms=float(sys.argv[4]) if len(sys.argv)>4 else 120.0
    x,sx=readwav(sweep_p); y,sy=readwav(rec_p)
    assert sx==sy==48000, "esperaba 48 kHz (sweep %d, rec %d)"%(sx,sy)
    N=1
    while N < len(x)+len(y): N*=2
    X=np.fft.rfft(x,N); Y=np.fft.rfft(y,N)
    eps=1e-6*np.max(np.abs(X))**2
    H=(Y*np.conj(X))/(np.abs(X)**2+eps)
    h=np.fft.irfft(H,N)
    # IR sits at the start; window around the main peak
    pk=int(np.argmax(np.abs(h[:sx//2])))
    pre=int(0.001*sx)                       # 1 ms pre-roll
    L=int(ir_ms/1000.0*sx)
    s=max(0,pk-pre); seg=h[s:s+L].copy()
    fo=int(0.2*len(seg)); seg[-fo:]*=np.linspace(1,0,fo)**2   # smooth tail fade
    seg/= (np.max(np.abs(seg))+1e-12); seg*=0.95             # peak-normalize -0.45 dBFS
    write_f32(out_p, seg)
    print("wrote %s  %d samples (%.0f ms)  peak 0.95  (cab IR, mono f32 48k)"%(out_p,len(seg),len(seg)/sx*1000))
if __name__=="__main__": main()
