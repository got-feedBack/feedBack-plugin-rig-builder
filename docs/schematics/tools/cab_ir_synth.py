#!/usr/bin/env python3
"""Sintetiza IRs de cab ORIGINALES (100% nuestros, distribuibles) desde una curva
de respuesta DISEÑADA por anclas (informada por acústica de cabs reales), via
reconstrucción de FASE MÍNIMA. No usa samples de nadie — es nuestro filtro.

Salida: IR mono 48 kHz float32, ~43 ms, pico 0.95 (formato del engine).
Uso: cab_ir_synth.py <voicing> <out.wav>     (voicing: ver VOICINGS)
"""
import sys, struct, numpy as np
SR=48000; NFFT=16384; IR_LEN=2048

# Cada voicing = anclas (Hz, dB rel) de la respuesta del cab+mic. DISEÑO propio.
VOICINGS = {
 # 4x12 con V30, close-mic dinámico (estilo 57). Curva diseñada hacia un 4x12 real
 # microfoneado: pico de cuerpo ~180 Hz, scoop de medios, rolloff ~5-6 kHz.
 "4x12_v30_dyn": [(25,-20),(40,-12),(85,-3),(180,2.5),(350,-1.3),(700,-3.8),
                  (1400,-4.2),(2800,-2.5),(5000,-3.1),(7700,-19),(12500,-33),(20000,-42)],
}

def _min_phase(mag):
    full=np.concatenate([mag, mag[-2:0:-1]])              # full spectrum (len NFFT)
    cep=np.fft.ifft(np.log(np.maximum(full,1e-9))).real   # real cepstrum
    w=np.zeros(NFFT); w[0]=1; w[1:NFFT//2]=2; w[NFFT//2]=1 # min-phase window
    mp=np.exp(np.fft.fft(cep*w))
    ir=np.fft.ifft(mp).real[:IR_LEN]
    fo=int(0.25*IR_LEN); ir[-fo:]*=np.linspace(1,0,fo)**2  # tail fade
    ir/=(np.max(np.abs(ir))+1e-12); ir*=0.95
    return ir

def synth(anchors):
    f=np.fft.rfftfreq(NFFT,1/SR)
    fa=np.array([a[0] for a in anchors]); da=np.array([a[1] for a in anchors])
    db=np.interp(np.log10(np.maximum(f,1.0)), np.log10(fa), da, left=da[0], right=da[-1])
    return _min_phase(10**(db/20.0))

def _readir(p):
    b=open(p,'rb').read(); pos=12; doff=dsz=ft=nch=bps=None
    while pos+8<=len(b):
        cid=b[pos:pos+4]; sz=struct.unpack('<I',b[pos+4:pos+8])[0]
        if cid==b'fmt ': ft,nch,_,_,_,bps=struct.unpack('<HHIIHH',b[pos+8:pos+8+16])
        elif cid==b'data': doff,dsz=pos+8,sz
        pos+=8+sz+(sz&1)
    raw=b[doff:doff+dsz]
    if   ft==3 and bps==32: a=np.frombuffer(raw,'<f4').astype(float)
    elif ft==1 and bps==24:
        x=np.frombuffer(raw,np.uint8).reshape(-1,3).astype(np.int32); a=((x[:,0]|(x[:,1]<<8)|(x[:,2]<<16))<<8>>8)/8388608.0
    elif ft==1 and bps==16: a=np.frombuffer(raw,'<i2').astype(float)/32768
    else: a=np.frombuffer(raw,'<i4').astype(float)/2147483648
    if nch and nch>1: a=a.reshape(-1,nch).mean(1)
    return a

def synth_from_ir(target_ir, frac=6):
    """Deriva un IR propio (fase mínima) de la magnitud SUAVIZADA (1/frac octava)
    de un IR objetivo. El suavizado generaliza la curva (funcional) y quita ruido/
    comb de la captura -> el resultado es nuestro filtro, no sus samples."""
    h=_readir(target_ir)
    f=np.fft.rfftfreq(NFFT,1/SR)
    db=20*np.log10(np.abs(np.fft.rfft(h,NFFT))+1e-9)
    lo=np.searchsorted(f, f*2**(-1/(2*frac))); hi=np.searchsorted(f, f*2**(1/(2*frac)))
    cs=np.concatenate([[0.0],np.cumsum(db)]); cnt=np.maximum(hi-lo,1)
    sm=(cs[hi]-cs[lo])/cnt                                 # box-avg dB over 1/6 oct
    return _min_phase(10**(sm/20.0))

def write_f32(p,x):
    x=x.astype('<f4'); ds=len(x)*4
    open(p,'wb').write(b'RIFF'+struct.pack('<I',36+ds)+b'WAVE'+b'fmt '+
        struct.pack('<IHHIIHH',16,3,1,SR,SR*4,4,32)+b'data'+struct.pack('<I',ds)+x.tobytes())

if __name__=="__main__":
    if sys.argv[1]=="--from-ir":
        target=sys.argv[2]; out=sys.argv[3]
        write_f32(out, synth_from_ir(target))
        print("wrote %s  (from-ir %s, %d samples / %.0f ms, min-phase 1/6-oct, original)"%(out,target.split('/')[-1],IR_LEN,IR_LEN/SR*1000))
    else:
        v=sys.argv[1]; out=sys.argv[2]
        write_f32(out, synth(VOICINGS[v]))
        print("wrote %s  (%s, %d samples / %.0f ms, min-phase, original)"%(out,v,IR_LEN,IR_LEN/SR*1000))
