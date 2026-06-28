import subprocess, os, re, statistics
RB="/Users/nacho/Files/slopsmith/rig_builder"; SH=f"{RB}/vst/src/_shared/tube_stage.hpp"
DI="/Users/nacho/Files/slopsmith/ui_public_inputs_Brit - Guitar.wav"
EN30="setInput setNormalVol setTBVol setTreble setBass setBright setCut setMaster".split()
TW26="setTone setInstVol setMicVol setBright setBass setPresence".split()
TW22="setBurnBass setBurnMid setBurnTreble setBurnVol setChannel setGain1 setGain2 setNormFat setPresence setReverb setVintBass setVintTreble setVintVol".split()
# dir, core, params, header, sig, p32, K, ns, setup_kind
A=[
("en30","BoxDC30Core","EN30Params.h","BoxDC30Core.h","mono","core",0.505,"boxdc30","en30"),
("tw26","TW26Core","TW26Params.h","TW26Core.h","mono","core",0.522,"tw26","tw26"),
("tw22","TW22Core","TW22Params.h","TW22Core.h","mono","plugin",0.476,"tw22","tw22"),
("tw40","TW40Core","TW40Params.h","TW40Core.h","mono","core",0.600,"tw40","param"),
("plexi","PlexiCore","PlexiParams.h",None,"mono","plugin",0.550,"","param"),
("jcm800_marsten","Jcm800Core","Jcm800Params.h",None,"mono","plugin",0.560,"","param"),
("dual_rect","DualRectCore","DualRectParams.h","DualRectCore.h","mono","plugin",0.470,"dualrect","param"),
("dsl100","DSL100Core","DSL100Params.h",None,"mono","plugin",0.638,"","param"),
("mark_iii","MarkIIICore","MarkIIIParams.h",None,"mono","core",0.560,"","param"),
("mark_ii","MarkIICore","MarkIIParams.h",None,"mono","plugin",0.560,"","param"),
("aor50","AOR50Core","AOR50Params.h",None,"mono","plugin",0.600,"","param"),
("dr103_lovolt","Dr103Core","Dr103Params.h",None,"mono","core",0.560,"","param"),
("dr504_lovolt","Dr504Core","Dr504Params.h",None,"mono","core",0.560,"","param"),
("jc90","JC90Core","JC90Params.h",None,"stereo","plugin",0.640,"","param"),
("jc120_ronald","JC120Core","JC120Params.h",None,"stereo","plugin",0.560,"","param"),
]
def hdrf(d,core,params,hdr):
    out=f"/tmp/m_{d}.h"; amp=f"{RB}/vst/src/amps/{d}"
    L=["#include <cmath>","#include <cstring>",f'#include "{amp}/{params}"',f'#include "{SH}"']
    if hdr: L.append(f'#include "{amp}/{hdr}"')
    else:
        src=open(f"{amp}/{[f for f in os.listdir(amp) if f.endswith('Plugin.cpp')][0]}").read().splitlines()
        ns=next(i for i,l in enumerate(src) if l.startswith("namespace {"))
        pl=next(i for i,l in enumerate(src) if re.match(r"^class \w+Plugin",l))
        L+=src[ns:pl-1]
    open(out,"w").write("\n".join(L)+"\n"); return out
def setup(kind):
    if kind=="param": return "for(int i=0;i<kParamCount;i++)c.setParam(i,0.5f);"
    s={"en30":EN30,"tw26":TW26,"tw22":TW22}[kind]
    return "".join(f"c.{m}(0.5f);" for m in s)
def run(d,core,params,hdr,sig,p32,K,ns,kind):
    h=hdrf(d,core,params,hdr); feed="3.2f*x" if p32=="plugin" else "x"
    proc="float oL,oR;c.process(in0,in0,oL,oR);float y=oL;" if sig=="stereo" else "float y=c.process(in0);"
    ctype=(ns+"::" if ns else "")+core
    cpp=f'''#include "{h}"
#include <cstdio>
#include <cstdint>
#include <vector>
using namespace std;
static inline float rbAmpLvl(float x){{const float t=0.90f,c=0.99f,a=(x<0.f?-x:x);if(a<=t)return x;return (x<0.f?-1.f:1.f)*(t+(c-t)*tanhf((a-t)/(c-t)));}}
static vector<float> rd(const char*p,int&sr){{FILE*f=fopen(p,"rb");vector<float> o;if(!f)return o;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);vector<uint8_t> b(n);fread(b.data(),1,n,f);fclose(f);int af=1,ch=1,bps=16;sr=48000;long pos=12,doff=0,dsz=0;
 while(pos+8<=n){{uint32_t id=*(uint32_t*)&b[pos],sz=*(uint32_t*)&b[pos+4];if(id==0x20746d66){{af=*(uint16_t*)&b[pos+8];ch=*(uint16_t*)&b[pos+10];sr=*(int*)&b[pos+12];bps=*(uint16_t*)&b[pos+22];}}else if(id==0x61746164){{doff=pos+8;dsz=sz;}}pos+=8+sz+(sz&1);}}
 int by=bps/8;long fr=dsz/(by*ch);for(long i=0;i<fr;i++){{double s=0;for(int cc=0;cc<ch;cc++){{const uint8_t*q=&b[doff+(i*ch+cc)*by];double v=0;if(af==3&&bps==32)v=*(float*)q;else if(bps==16)v=*(int16_t*)q/32768.0;else if(bps==24){{int32_t x=(q[0]|(q[1]<<8)|(q[2]<<16));if(x&0x800000)x|=~0xFFFFFF;v=x/8388608.0;}}else if(bps==32)v=*(int32_t*)q/2147483648.0;s+=v;}}o.push_back((float)(s/ch));}}return o;}}
int main(){{int sr;auto di=rd("{DI}",sr);{ctype} c;c.setSampleRate((float)sr);{setup(kind)}
 double a=0;long n=0;for(float x:di){{float in0={feed};{proc} float o=rbAmpLvl({K}f*y);a+=o*o;n++;}}
 printf("{d}|%.2f\\n",20*log10(sqrt(a/n)+1e-12));return 0;}}
'''
    cf=f"/tmp/m_{d}.cpp"; open(cf,"w").write(cpp)
    r=subprocess.run(["c++","-O2","-std=c++17",cf,"-o",f"/tmp/m_{d}"],capture_output=True,text=True)
    if r.returncode!=0: return f"{d}|FAIL "+(r.stderr.strip().splitlines()[-1] if r.stderr else "")
    return subprocess.run([f"/tmp/m_{d}"],capture_output=True,text=True).stdout.strip()
rows=[]
for cfg in A:
    o=run(*cfg)
    if re.match(r"^[\w]+\|-?\d",o): d,db=o.split("|"); rows.append((d,float(db)))
    else: print("  ",o)
print("\n=== LOUDNESS @ TODOS LOS KNOBS 0.5 (RMS dBFS, mismo DI + K + rbAmpLvl) ===")
for d,db in sorted(rows,key=lambda x:-x[1]): print(f"  {d:16s} {db:7.2f}")
ds=[db for _,db in rows]; med=statistics.median(ds)
print(f"\n  mediana {med:.2f} | rango {max(ds)-min(ds):.1f} dB")
