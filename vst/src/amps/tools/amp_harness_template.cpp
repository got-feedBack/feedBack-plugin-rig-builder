// amp_harness_template.cpp — plantilla de banco de pruebas OFFLINE para calibrar un amp.
//
// Corre el Core del amp (sin abrir Slopsmith) sobre la Brit DI y mide RMS + CREST
// (pico/RMS en dB) = el proxy de distorsión. Crest alto = limpio; bajo = saturado.
// También verifica estabilidad (NaN/inf a 48/96/192 kHz).
//
// USO (el agente lo adapta por amp):
//   1) Extraer el Core inline del Plugin.cpp a un header temporal:
//        NS=$(grep -nE '^namespace \{' XxxPlugin.cpp | head -1 | cut -d: -f1)
//        END=$(( $(grep -nE '^class XxxPlugin' XxxPlugin.cpp | head -1 | cut -d: -f1) - 1 ))
//        { echo '#include <cmath>'; echo '#include "XxxParams.h"';
//          echo '#include ".../_shared/tube_stage.hpp"';
//          sed -n "${NS},${END}p" XxxPlugin.cpp; } > /tmp/core_test.h
//      (si el Core ya está en su propio .h, incluirlo directo y saltar este paso)
//   2) Ajustar: el #include, el tipo del Core, los kXxx params, y el barrido.
//   3) c++ -O2 -std=c++17 harness.cpp -o harness && ./harness
//
#include "/tmp/core_test.h"           // <-- el header del Core extraído
#include <cstdio>
#include <cstdint>
#include <vector>
#include <initializer_list>
using namespace std;
using CoreT = XxxCore;                 // <-- el tipo del Core
static const char* DI = "/Users/nacho/Files/slopsmith/ui_public_inputs_Brit - Guitar.wav";

static vector<float> rd(const char*p,int&sr){FILE*f=fopen(p,"rb");vector<float> o;if(!f)return o;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);vector<uint8_t> b(n);fread(b.data(),1,n,f);fclose(f);int af=1,ch=1,bps=16;sr=48000;long pos=12,doff=0,dsz=0;
 while(pos+8<=n){uint32_t id=*(uint32_t*)&b[pos],sz=*(uint32_t*)&b[pos+4];if(id==0x20746d66){af=*(uint16_t*)&b[pos+8];ch=*(uint16_t*)&b[pos+10];sr=*(int*)&b[pos+12];bps=*(uint16_t*)&b[pos+22];}else if(id==0x61746164){doff=pos+8;dsz=sz;}pos+=8+sz+(sz&1);}
 int by=bps/8;long fr=dsz/(by*ch);for(long i=0;i<fr;i++){double s=0;for(int c=0;c<ch;c++){const uint8_t*q=&b[doff+(i*ch+c)*by];double v=0;if(af==3&&bps==32)v=*(float*)q;else if(bps==16)v=*(int16_t*)q/32768.0;else if(bps==24){int32_t x=(q[0]|(q[1]<<8)|(q[2]<<16));if(x&0x800000)x|=~0xFFFFFF;v=x/8388608.0;}else if(bps==32)v=*(int32_t*)q/2147483648.0;s+=v;}o.push_back((float)(s/ch));}return o;}
static void st(const vector<float>&s,double&r,double&c){double a=0,pk=0;for(float x:s){a+=x*x;if(fabs(x)>pk)pk=fabs(x);}r=sqrt(a/s.size());c=20*log10(pk/(r+1e-12));}

int main(){
  // 1) estabilidad: NaN/inf + pico a cada sample rate (¡debe quedar acotado y sin NaN!)
  for(float SR:{48000.f,96000.f,192000.f}){ CoreT c; c.setSampleRate(SR);
    // c.setParam(kGain, 0.8f); ... configurar un caso "hot"
    double pk=0; bool nan=false; int n=(int)(SR*2);
    for(int i=0;i<n;i++){ float x=1.5f*sinf(2*M_PI*150*i/SR)*(0.5f+0.5f*sinf(2*M_PI*0.7f*i/SR));
      float y=c.process(x);                       // <-- ajustar firma (mono / estéreo)
      if(std::isnan(y)||std::isinf(y)){nan=true;break;} if(fabs(y)>pk)pk=fabs(y); }
    printf("SR=%.0f NaN=%s peak=%.3f\n",SR,nan?"YES":"no",pk); }

  // 2) curva de crest vs el knob de Gain (debe ser MONOTÓNICA: gain bajo=limpio -> alto=sucio)
  int sr; auto di=rd(DI,sr);
  printf("\nGain | rms / crest\n");
  for(int g:{1,4,7,10}){ CoreT c; c.setSampleRate((float)sr);
    // c.setParam(kGain, g/10.f); ... + tono neutro (0.5), master, etc.
    vector<float> o; for(float x:di) o.push_back(c.process(3.2f*x));  // 3.2x = nudge de entrada en vivo
    double r,cr; st(o,r,cr); printf("  g=%-2d | %.4f / %5.1f\n",g,r,cr); }
  return 0;
}
