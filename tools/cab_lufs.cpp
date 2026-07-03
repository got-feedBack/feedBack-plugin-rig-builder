// cab_lufs.cpp — final stage of the offline tone-chain measurement.
// Reads a mono float32 signal from stdin, convolves it with a raw-float32 cab
// IR (argv[1]), applies a linear makeup gain (argv[2], e.g. 4.0 = the RS-IR
// +12 dB), and prints the BS.1770-4 integrated LUFS of the result to stdout.
//
//   cab_lufs <ir_raw_f32_path> <gain>   < signal_f32   ->   prints "<LUFS>"
//
// Build: clang++ -std=c++14 -O2 tools/cab_lufs.cpp -o <bin>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static const double SR = 48000.0;
static const int BLOCK = 19200, HOP = 4800;

struct BQ { double b0,b1,b2,a1,a2,z1=0,z2=0; double p(double x){double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;} };

static double integratedLUFS(const std::vector<float>& x) {
    BQ s1{1.53512485958697,-2.69169618940638,1.19839281085285,-1.69065929318241,0.73248077421585};
    BQ s2{1.0,-2.0,1.0,-1.99004745483398,0.99007225036621};
    std::vector<double> w(x.size());
    for (size_t i=0;i<x.size();++i) w[i]=s2.p(s1.p((double)x[i]));
    std::vector<double> z;
    for (size_t s=0;s+BLOCK<=w.size();s+=HOP){ double ms=0; for(int j=0;j<BLOCK;++j) ms+=w[s+j]*w[s+j]; z.push_back(ms/BLOCK); }
    if (z.empty()) return -120.0;
    auto L=[](double ms){ return ms>0?-0.691+10*std::log10(ms):-120.0; };
    double acc=0; int c=0;
    for(double ms:z) if(L(ms)>=-70){acc+=ms;++c;}
    if(!c) return -120.0;
    double rel=L(acc/c)-10;
    acc=0;c=0;
    for(double ms:z) if(L(ms)>=-70&&L(ms)>=rel){acc+=ms;++c;}
    return c?L(acc/c):-120.0;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr,"usage: cab_lufs <ir.f32> <gain>\n"); return 2; }
    double gain = std::atof(argv[2]);

    std::vector<float> ir;
    if (FILE* f=std::fopen(argv[1],"rb")) {
        float b[4096]; size_t g;
        while ((g=std::fread(b,sizeof(float),4096,f))>0) ir.insert(ir.end(),b,b+g);
        std::fclose(f);
    }
    std::vector<float> sig;
    { float b[4096]; size_t g; while ((g=std::fread(b,sizeof(float),4096,stdin))>0) sig.insert(sig.end(),b,b+g); }

    std::vector<float> out;
    if (ir.empty()) {                       // no IR: passthrough * gain
        out = sig;
        for (auto& v : out) v *= (float)gain;
    } else {
        out.assign(sig.size()+ir.size(), 0.0f);   // direct convolution
        for (size_t i=0;i<sig.size();++i){
            const double s=(double)sig[i];
            if (s==0.0) continue;
            for (size_t k=0;k<ir.size();++k) out[i+k]+=(float)(s*ir[k]);
        }
        for (auto& v : out) v *= (float)gain;
    }
    std::printf("%.4f\n", integratedLUFS(out));
    return 0;
}
