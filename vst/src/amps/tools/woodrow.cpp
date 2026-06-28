#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
static std::vector<float> rd(const char*p,int&sr){FILE*f=fopen(p,"rb");std::vector<float> o;if(!f)return o;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);std::vector<uint8_t> b(n);fread(b.data(),1,n,f);fclose(f);int af=1,ch=1,bps=16;sr=48000;long pos=12,doff=0,dsz=0;
 while(pos+8<=n){uint32_t id=*(uint32_t*)&b[pos],sz=*(uint32_t*)&b[pos+4];if(id==0x20746d66){af=*(uint16_t*)&b[pos+8];ch=*(uint16_t*)&b[pos+10];sr=*(int*)&b[pos+12];bps=*(uint16_t*)&b[pos+22];}else if(id==0x61746164){doff=pos+8;dsz=sz;}pos+=8+sz+(sz&1);}
 int by=bps/8;long fr=dsz/(by*ch);for(long i=0;i<fr;i++){double s=0;for(int c=0;c<ch;c++){const uint8_t*q=&b[doff+(i*ch+c)*by];double v=0;if(af==3&&bps==32)v=*(float*)q;else if(bps==16)v=*(int16_t*)q/32768.0;else if(bps==24){int32_t x=(q[0]|(q[1]<<8)|(q[2]<<16));if(x&0x800000)x|=~0xFFFFFF;v=x/8388608.0;}else if(bps==32)v=*(int32_t*)q/2147483648.0;s+=v;}o.push_back((float)(s/ch));}return o;}
int main(int ac,char**av){for(int i=1;i<ac;i++){int sr;auto s=rd(av[i],sr);if(s.empty()){printf("%s: (vacio)\n",av[i]);continue;}
 double a=0,pk=0;for(float x:s){a+=x*x;if(fabs(x)>pk)pk=fabs(x);}double rms=sqrt(a/s.size());
 const char*b=av[i];const char*sl=b;for(const char*p=b;*p;p++)if(*p=='/')sl=p+1;
 printf("%-34s rms %.4f  crest %.1f dB\n",sl,rms,20*log10(pk/(rms+1e-12)));}return 0;}
