#ifndef DR504_CORE_H
#define DR504_CORE_H

// Early-70s Hiwatt DR504, reconstructed from the local Mark Huss schematics:
// V1 ECC83 Normal/Brilliant -> A500k volumes + 470k mixers -> V2 ECC83
// -> passive Hiwatt TMB -> L250k master -> V3 ECC83 gain/bias follower
// -> fixed-bias ECC81 LTP -> 2x EL34 -> Partridge TG6556/TH7551 output transformer.

#include "../../_shared/tube_stage.hpp"
#include <algorithm>
#include <cmath>

namespace dr504 {

static constexpr float kPi = 3.14159265358979f;
static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

struct Biquad {
    float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    inline float process(float x) {
        const float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
        x2=x1; x1=x; y2=y1; y1=rbtube::dn(y); return y;
    }
    void reset() { x1=x2=y1=y2=0.0f; }
    void highShelf(float sr,float f,float dB) {
        f=std::fmin(f,sr*0.49f);
        const float A=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*f/sr,c=std::cos(w),s=std::sin(w);
        const float al=s*0.70710678f,rA=std::sqrt(A),q=2.0f*rA*al;
        const float d=(A+1.0f)-(A-1.0f)*c+q;
        b0=A*((A+1.0f)+(A-1.0f)*c+q)/d;
        b1=-2.0f*A*((A-1.0f)+(A+1.0f)*c)/d;
        b2=A*((A+1.0f)+(A-1.0f)*c-q)/d;
        a1=2.0f*((A-1.0f)-(A+1.0f)*c)/d;
        a2=((A+1.0f)-(A-1.0f)*c-q)/d;
    }
    void peaking(float sr,float f,float q,float dB) {
        f=std::fmin(f,sr*0.49f);
        const float A=std::pow(10.0f,dB/40.0f),w=2.0f*kPi*f/sr,c=std::cos(w),al=std::sin(w)/(2.0f*q);
        const float d=1.0f+al/A;
        b0=(1.0f+al*A)/d; b1=-2.0f*c/d; b2=(1.0f-al*A)/d;
        a1=-2.0f*c/d; a2=(1.0f-al/A)/d;
    }
};

// Backward-Euler MNA of the actual Hiwatt tone network. Unlike a Marshall/Fender
// FMV stack this circuit has the 1n/220p treble bridge, 1n mid bypass and two
// 47n bass branches shown in DR_Pre4Input_v1a. The 220k master and 1M V3 grid
// load are part of the solve, so control interaction and insertion loss are real.
struct HiwattToneNetwork {
    enum { X, A, T, B, W, C, D, O, M, S, N };
    enum { C_XA, C_AT, C_CD, C_XD, C_XG, NCAPS };
    double inv[N][N]{};
    double capG[NCAPS]{}, capV[NCAPS]{};
    double sourceG=1.0/47000.0;

    static void stampG(double a[N][N],int n1,int n2,double g) {
        if(n1>=0) a[n1][n1]+=g;
        if(n2>=0) a[n2][n2]+=g;
        if(n1>=0&&n2>=0){ a[n1][n2]-=g; a[n2][n1]-=g; }
    }
    static double conductance(double r) { return 1.0/std::fmax(1.0,r); }
    static void invert(double a[N][N],double out[N][N]) {
        double q[N][2*N]{};
        for(int r=0;r<N;++r) for(int c=0;c<N;++c){ q[r][c]=a[r][c]; q[r][N+c]=(r==c); }
        for(int c=0;c<N;++c){
            int p=c; for(int r=c+1;r<N;++r) if(std::fabs(q[r][c])>std::fabs(q[p][c])) p=r;
            if(p!=c) for(int k=0;k<2*N;++k) std::swap(q[p][k],q[c][k]);
            double d=q[c][c]; if(std::fabs(d)<1.0e-18) d=(d<0?-1.0:1.0)*1.0e-18;
            for(int k=0;k<2*N;++k) q[c][k]/=d;
            for(int r=0;r<N;++r) if(r!=c){ const double m=q[r][c]; for(int k=0;k<2*N;++k) q[r][k]-=m*q[c][k]; }
        }
        for(int r=0;r<N;++r) for(int c=0;c<N;++c) out[r][c]=q[r][N+c];
    }
    void set(float sr,float bass,float treble,float middle,float master) {
        double a[N][N]{};
        sourceG=conductance(47000.0);             // V2 plate/source impedance
        a[X][X]+=sourceG;
        stampG(a,X,S,conductance(100000.0));      // Hiwatt slope resistor
        stampG(a,A,B,conductance(220000.0));

        const double tp=std::fmax(0.00001,std::fmin(0.99999,(double)treble));
        stampG(a,T,W,conductance((1.0-tp)*250000.0));
        stampG(a,W,B,conductance(tp*250000.0));
        stampG(a,B,C,conductance(22000.0));

        // The Middle wiper is strapped as a rheostat. Clockwise rotation raises
        // its C-D resistance and restores the Hiwatt mid band; reversing this
        // term makes the front-panel Middle control behave backwards.
        stampG(a,C,D,conductance(1.0+(double)middle*100000.0));
        const double bp=rbtube::PotTaper::audio(bass,1.70f);
        stampG(a,D,-1,conductance(1.0+bp*500000.0));
        stampG(a,W,O,conductance(22000.0));

        // The DR504 layout identifies the Master as 250k linear. Keep the
        // electrical orientation used by the real wiper, but do not impose the
        // DR103 audio taper here.
        const double mp=std::fmax(0.00001,std::fmin(0.99999,(double)master));
        stampG(a,O,M,conductance((1.0-mp)*250000.0));
        stampG(a,M,-1,conductance(mp*250000.0));
        stampG(a,M,-1,conductance(1000000.0));    // V3 grid leak

        const double caps[NCAPS]={1.0e-9,220.0e-12,1.0e-9,47.0e-9,47.0e-9};
        const int n1[NCAPS]={X,A,C,S,S};
        const int n2[NCAPS]={A,T,D,D,-1};
        for(int i=0;i<NCAPS;++i){ capG[i]=caps[i]*(double)sr; stampG(a,n1[i],n2[i],capG[i]); }
        invert(a,inv);
    }
    static void addHistory(double rhs[N],int a,int b,double g,double v) {
        if(a>=0) rhs[a]+=g*v;
        if(b>=0) rhs[b]-=g*v;
    }
    inline float process(float input) {
        double rhs[N]{};
        rhs[X]+=sourceG*(double)input;
        addHistory(rhs,X,A,capG[C_XA],capV[C_XA]);
        addHistory(rhs,A,T,capG[C_AT],capV[C_AT]);
        addHistory(rhs,C,D,capG[C_CD],capV[C_CD]);
        addHistory(rhs,S,D,capG[C_XD],capV[C_XD]);
        addHistory(rhs,S,-1,capG[C_XG],capV[C_XG]);
        double v[N]{};
        for(int r=0;r<N;++r) for(int c=0;c<N;++c) v[r]+=inv[r][c]*rhs[c];
        capV[C_XA]=v[X]-v[A]; capV[C_AT]=v[A]-v[T]; capV[C_CD]=v[C]-v[D];
        capV[C_XD]=v[S]-v[D]; capV[C_XG]=v[S];
        return rbtube::dn((float)v[M]);
    }
    void reset() { for(double &v:capV) v=0.0; }
};

// V3B is the low-output-impedance cathode follower that biases the unusual
// fixed-bias Hiwatt PI. It transfers the early-70s circuit's AC signal while
// remaining much cleaner than another common-cathode gain stage.
struct BiasCathodeFollower {
    rbtube::HP1 coupling;
    rbtube::LP1 pole;
    float current=0.0f,attack=0.0f,release=0.0f;
    void set(float sr) {
        coupling.set(sr,34.0f);                 // 47n into the 100k follower grid leak
        pole.set(sr,20000.0f);
        attack=1.0f-std::exp(-1.0f/(0.0018f*sr));
        release=1.0f-std::exp(-1.0f/(0.075f*sr));
    }
    inline float process(float x) {
        const float g=pole.process(coupling.process(x));
        const float positive=std::fmax(0.0f,g-1.15f);
        current+=(positive-current)*(positive>current?attack:release);
        const float y=g/(1.0f+0.22f*current);
        return y>=0.0f ? 2.8f*std::tanh(y/2.8f) : 3.4f*std::tanh(y/3.4f);
    }
    void reset(){ coupling.reset(); pole.reset(); current=0.0f; }
};

// The DR504 does not self-bias V4 like the reusable Fender/Marshall LTP. V3B
// holds both ECC81 grids at a fixed DC voltage while the 22k tail establishes
// the operating current. Model both plate swings around that fixed point from
// the generated 12AT7 table. Solving it as a cathode-biased stage against the
// 460V rail incorrectly drives the clean signal into cut-off.
struct HiwattFixedBiasPI {
    rbtube::HP1 inputCoupling;
    rbtube::LP1 inputPole,tailEnvelope,outputPole;
    float driveToGrid=44.0f,plateScale=1.0f,bias=-2.10f,biasMotion=0.0f;
    float biasAttack=0.0f,biasRelease=0.0f;

    static float softLimit(float v) {
        constexpr float knee=.40f;
        constexpr float lo=rbtube::Tube12AT7::vmin+.10f;
        constexpr float hi=rbtube::Tube12AT7::vmax-.10f;
        if(v>hi-knee) return (hi-knee)+knee*std::tanh((v-(hi-knee))/knee);
        if(v<lo+knee) return (lo+knee)+knee*std::tanh((v-(lo+knee))/knee);
        return v;
    }
    void set(float sr,float driveV,float outV) {
        inputCoupling.set(sr,34.0f);          // 47n coupling into loaded PI grid
        inputPole.set(sr,18500.0f);           // ECC81 Miller + wiring capacitance
        tailEnvelope.set(sr,13.0f);           // 22k + loaded 2k2 feedback tail
        outputPole.set(sr,22000.0f);
        driveToGrid=44.0f*driveV;
        plateScale=outV;
        biasAttack=1.0f-std::exp(-1.0f/(0.0015f*sr));
        biasRelease=1.0f-std::exp(-1.0f/(0.085f*sr));
    }
    inline float process(float x) {
        const float d=inputPole.process(inputCoupling.process(x))*driveToGrid;

        // The follower holds the quiescent bias stable. Only large excursions
        // create shared-tail/grid-current bias motion and compression.
        const float env=tailEnvelope.process(std::fabs(d));
        const float over=std::fmax(0.0f,env-1.55f);
        const float target=std::fmin(.85f,.20f*over);
        biasMotion+=(target-biasMotion)*(target>biasMotion?biasAttack:biasRelease);
        const float b=bias-biasMotion;

        constexpr float loadA=82000.0f/86500.0f;
        constexpr float loadB=91000.0f/86500.0f;
        const float rest=rbtube::Tube12AT7::ftube(1,b);
        const float a=(rbtube::Tube12AT7::ftube(1,softLimit(b+d))-rest)*loadA;
        const float c=(rbtube::Tube12AT7::ftube(1,softLimit(b-d))-rest)*loadB;
        return rbtube::dn(outputPole.process((c-a)*plateScale));
    }
    void reset() {
        inputCoupling.reset(); inputPole.reset(); tailEnvelope.reset(); outputPole.reset();
        biasMotion=0.0f;
    }
};

struct Dr504Core {
    float sr=192000.0f;
    float pNormal=.5f,pBrilliant=.55f,pBass=.5f,pTreble=.5f,pMiddle=.5f;
    float pPresence=.5f,pMaster=.55f,pInput=.5f,pCab=0.0f;
    float normalPot=.0f,brightPot=.0f,normalWeight=1.0f,brightWeight=1.0f;
    float lastPowerLoad=0,lastScreenLoad=0,lastPreampLoad=0,lastOt=0,outMakeup=1.0f;
    float outputScale=.18f;

    rbtube::HP1 inputCoupling,brightCouple,normalCouple;
    rbtube::LP1 feedbackLow,cabLP1,cabLP2;
    rbtube::HP1 cabHP;
    rbtube::TubeStage v1Bright,v1Normal,v2Bright,v2Normal,v3Gain;
    rbtube::Miller12AX7 mV1Bright,mV1Normal,mV2Bright,mV2Normal,mV3;
    HiwattToneNetwork tone;
    BiasCathodeFollower biasFollower;
    HiwattFixedBiasPI pi;
    rbtube::CouplingCapGridLeak piToPower;
    rbtube::MultiNodeBPlus supply;
    rbtube::PowerAmpEL34 power;
    Biquad presenceShape,otLowBody,otBody,otMidContour,otUpperBody,otAir,cabPresence;

    void setSampleRate(float s){ sr=s>1000.0f?s:192000.0f; recalc(); reset(); }
    void setNormal(float v){pNormal=clamp01(v);recalc();}
    void setBrilliant(float v){pBrilliant=clamp01(v);recalc();}
    void setBass(float v){pBass=clamp01(v);recalc();}
    void setTreble(float v){pTreble=clamp01(v);recalc();}
    void setMiddle(float v){pMiddle=clamp01(v);recalc();}
    void setPresence(float v){pPresence=clamp01(v);recalc();}
    void setMaster(float v){pMaster=clamp01(v);recalc();}
    void setInput(float v){pInput=clamp01(v);recalc();}
    void setCabSim(float v){pCab=clamp01(v);}
    float outputMakeup() const { return outMakeup; }

    static float interpolate(const float* table,float v){
        const float p=10.0f*clamp01(v); int i=(int)p;
        if(i>=10) return table[10];
        return table[i]+(table[i+1]-table[i])*(p-(float)i);
    }

    void recalc(){
        inputCoupling.set(sr,2.5f);
        // V1 shares the schematic's 1k5 cathode / 100u bypass network. Above
        // roughly 1Hz both input halves retain their full low-frequency gain.
        v1Bright.setWithPlate(sr,1,300.0f,54.0f,1.06f,1500.0f,220000.0f);
        v1Normal.setWithPlate(sr,1,300.0f,54.0f,1.06f,1500.0f,220000.0f);
        mV1Bright.set(sr,68000.0f,58.0f,8.0f);
        mV1Normal.set(sr,68000.0f,58.0f,8.0f);

        // The 1n coupling cap sees the 470k volume network plus the finite V1
        // source impedance, so its effective corner is below 1/(2*pi*470k*C).
        brightCouple.set(sr,250.0f);
        normalCouple.set(sr,1.0f/(2.0f*kPi*500000.0f*22.0e-9f));
        // The reference plug-in keeps a small usable floor and stops short of
        // the electrically unloaded endpoint. The element itself remains the
        // schematic's A470k law over that calibrated wiper range.
        brightPot=rbtube::PotTaper::audio(.38f+.47f*pBrilliant,1.70f);
        normalPot=rbtube::PotTaper::audio(.38f+.47f*pNormal,1.70f);
        normalWeight=pInput<.75f?1.0f:0.0f;
        brightWeight=pInput>=.25f?1.0f:0.0f;

        // The channel wipers feed separate V2 halves. Their plates mix at the
        // shared 100k load; each cathode uses 1k with the common 47n network.
        v2Bright.setWithPlate(sr,1,345.0f,55.0f,3386.0f,1000.0f,100000.0f);
        v2Normal.setWithPlate(sr,1,345.0f,55.0f,3386.0f,1000.0f,100000.0f);
        mV2Bright.set(sr,470000.0f,54.0f,8.0f);
        mV2Normal.set(sr,470000.0f,54.0f,8.0f);
        tone.set(sr,pBass,pTreble,pMiddle,pMaster);

        // V3A is 100k/1k5 unbypassed. Its 47n part is the coupling capacitor
        // into V3B, not a cathode bypass capacitor.
        v3Gain.setWithPlate(sr,1,400.0f,47.0f,20000.0f,1500.0f,100000.0f);
        mV3.set(sr,220000.0f,56.0f,8.0f);
        biasFollower.set(sr);

        // V4 is ECC81, not an ideal splitter: 82k/91k plates and the fixed-bias
        // 22k + 2k2 tail. The follower keeps the pair centered and very clean.
        const float hot=std::pow(std::fmax(brightPot,normalPot),1.85f);
        pi.set(sr,1.32f+1.25f*hot,1.0f);
        feedbackLow.set(sr,1550.0f);
        presenceShape.highShelf(sr,1550.0f,-6.0f+12.0f*pPresence);
        piToPower.set(sr,100000.0f,47.0e-9f,22000.0f,.16f,.020f,.16f);

        // BYX94 bridge, 2x220u series reservoir, 100R HT2 drop, 1k screen
        // resistors per EL34 and 1k HT3 drop. The pair draws less reservoir
        // current than the DR103 quartet, while its screen nodes compress more.
        supply.set(sr,16.0f,110.0f,1100.0f,100.0f,1000.0f,220.0f,
                   .075f,.065f,.028f,.12f);
        // The 50W pair reaches grid/power compression earlier: -36V fixed bias
        // and the smaller TG6556/TH7551 transformer replace the DR103's quartet.
        power.set(sr,4.80f+5.20f*hot,-36.0f,.090f,38.0f,17500.0f);
        power.out=.0064f;
        power.biasShift=.85f;

        // Partridge TG6556/TH7551 primary/leakage response under the two-EL34
        // load. It retains the DR103 family balance but has a little less deep
        // extension and reaches magnetic compression earlier.
        otLowBody.peaking(sr,125.0f,.53f,7.8f);
        const bool normalOnly=pInput<.25f,brightOnly=pInput>.75f;
        otBody.peaking(sr,470.0f,1.0f,normalOnly?4.2f:4.9f);
        otMidContour.peaking(sr,1000.0f,.68f,normalOnly?-3.3f:(brightOnly?-1.0f:-1.8f));
        otUpperBody.peaking(sr,2200.0f,.80f,normalOnly?1.2f:(brightOnly?2.1f:1.5f));
        const float channelAir=normalOnly?-4.0f:(brightOnly?-.3f:-1.0f);
        otAir.highShelf(sr,3500.0f,5.5f+channelAir-2.0f*hot);

        cabHP.set(sr,82.0f); cabLP1.set(sr,5000.0f); cabLP2.set(sr,6400.0f);
        cabPresence.highShelf(sr,2700.0f,2.0f);

        // Matched-RMS compensation for the three input routings. The values are
        // dB measured at the final circuit output from the exact Brit DI. They
        // are applied in the wrapper after this core, so V1, PI, EL34 drive,
        // feedback and sag still follow the real channel-volume controls.
        static const float normalDb[11]={
            -6.1842f,-6.5717f,-6.8784f,-7.1205f,-7.3063f,-7.4483f,
            -7.5618f,-7.6574f,-7.7396f,-7.8113f,-7.8774f
        };
        static const float brightDb[11]={
            -6.7138f,-7.0201f,-7.2655f,-7.4681f,-7.6405f,-7.7878f,
            -7.9120f,-8.0162f,-8.1048f,-8.1811f,-8.2455f
        };
        static const float bothDb[11]={
            -7.4372f,-7.4491f,-7.4635f,-7.4801f,-7.4984f,-7.5400f,
            -7.5793f,-7.6159f,-7.6492f,-7.6789f,-7.7041f
        };
        const bool normalOnlyOut=pInput<.25f,brightOnlyOut=pInput>.75f;
        // The song mapping keeps Normal at 0.4 and drives Brilliant across its
        // full range. Index the jumpered compensation from that real gain
        // control so distortion changes without moving song loudness.
        const float active=normalOnlyOut?pNormal:pBrilliant;
        const float* table=normalOnlyOut?normalDb:(brightOnlyOut?brightDb:bothDb);
        outMakeup=std::pow(10.0f,.05f*interpolate(table,active));
    }

    void reset(){
        inputCoupling.reset(); brightCouple.reset(); normalCouple.reset();
        v1Bright.reset(); v1Normal.reset(); v2Bright.reset(); v2Normal.reset(); v3Gain.reset();
        mV1Bright.reset(); mV1Normal.reset(); mV2Bright.reset(); mV2Normal.reset(); mV3.reset();
        tone.reset(); biasFollower.reset(); pi.reset(); piToPower.reset(); supply.reset(); power.reset();
        feedbackLow.reset(); presenceShape.reset(); otLowBody.reset(); otBody.reset(); otMidContour.reset(); otUpperBody.reset(); otAir.reset();
        cabHP.reset(); cabLP1.reset(); cabLP2.reset(); cabPresence.reset();
        lastPowerLoad=lastScreenLoad=lastPreampLoad=lastOt=0.0f;
    }

    inline float process(float in){
        const rbtube::SupplyScales bplus=supply.process(lastPowerLoad,lastScreenLoad,lastPreampLoad);
        const float x=inputCoupling.process(in);

        float b=v1Bright.process(mV1Bright.process(x)*1.10f*bplus.preamp);
        float n=v1Normal.process(mV1Normal.process(x)*1.10f*bplus.preamp);
        // The 1n Brilliant coupling network has less low-frequency energy but
        // feeds a slightly hotter grid signal than the Normal branch.
        b=brightCouple.process(b)*brightPot*brightWeight*1.38f;
        n=normalCouple.process(n)*normalPot*normalWeight;

        // V2 mixes actively at its shared plate. The finite source impedances
        // leave the Normal branch dominant when both inputs are linked.
        const bool both=brightWeight>.5f&&normalWeight>.5f;
        const float vb=v2Bright.process(mV2Bright.process(b)*1.45f*bplus.preamp);
        const float vn=v2Normal.process(mV2Normal.process(n)*1.45f*bplus.preamp);
        float y=both?.50f*(.82f*vb+1.18f*vn):(vb+vn);
        y=tone.process(y)*9.5f;               // passive insertion-loss recovery
        y=v3Gain.process(mV3.process(y)*2.25f*bplus.preamp);
        y=biasFollower.process(y);
        lastPreampLoad=.06f*std::fabs(y)+.025f*std::fmax(brightPot,normalPot);

        // Closed-loop output feedback enters the PI tail. Presence changes the
        // treble component of feedback; it is not a post-power tone control.
        const float fbLow=feedbackLow.process(lastOt);
        const float fbHigh=lastOt-fbLow;
        const float fb=0.00075f*(fbLow+(1.0f-.78f*pPresence)*fbHigh);
        // PowerAmpEL34 inverts the PI differential, so the returned OT sample
        // is added here to produce negative feedback at the PI input.
        y=pi.process((y+fb)*bplus.screen);
        y=presenceShape.process(y);
        y=piToPower.process(y,1.0f);
        lastScreenLoad=.18f*std::fabs(y)+.035f*std::fmax(brightPot,normalPot);
        y=power.process(y*bplus.power*bplus.screen);
        y=otAir.process(otUpperBody.process(otMidContour.process(otBody.process(otLowBody.process(y)))));
        lastOt=y;
        lastPowerLoad=.34f*std::fabs(y)+.06f*std::fmax(brightPot,normalPot);

        if(pCab>.0001f){
            const float cab=cabPresence.process(cabLP2.process(cabLP1.process(cabHP.process(y))))*1.40f;
            y+=pCab*(cab-y);
        }
        return rbtube::dn(y*outputScale);
    }
};

} // namespace dr504

#endif
