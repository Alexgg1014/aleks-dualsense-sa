#pragma once
// Alek's DualSense SA v2: HD haptics synthesizer (pure, no Windows, tested by test_ds.cpp).
// The DualSense's USB audio endpoint is 4-channel; channels 3 and 4 drive the left and right voice-coil actuators.
// Every effect is generated here as a waveform: short "hits" (shots, steps, impacts) plus continuous layers (engine,
// terrain, skid, weapon loops, the game's own rumble converted to low/high sines). 2.2: gunshots can also come from the
// player's own weapon sound files (Astro Bot's approach: the haptic is derived from the sound), see make_clip.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hx {
constexpr float TAU=6.28318530718f;

// A transient: pitch glides f0 -> f1, envelope = attack then exponential decay. Times in seconds, gain 0..1.
struct Hit{float f0,f1,dur,decay,noise,gain;};

// ---------- presets ----------
// Indexed by eWeaponType. Lighter guns are higher and shorter; heavy ones low, long and noisy.
inline Hit weapon_hit(unsigned type){
    switch(type){
    case 22:return {320,110,0.04f,0.012f,0.35f,0.85f};   // pistol: crisp crack
    case 23:return {240,120,0.03f,0.008f,0.25f,0.50f};   // silenced: soft tick
    case 24:return {260,65,0.08f,0.026f,0.35f,1.00f};    // desert eagle: heavy kick
    case 25:return {150,40,0.16f,0.055f,0.50f,1.00f};    // shotgun: deep boom
    case 26:return {130,38,0.18f,0.060f,0.60f,1.00f};    // sawn-off: deeper, dirtier
    case 27:return {170,50,0.12f,0.045f,0.45f,0.95f};    // combat shotgun
    case 28:case 32:return {300,170,0.025f,0.007f,0.30f,0.60f};   // uzi, tec9: fast light ticks
    case 29:return {260,140,0.03f,0.009f,0.30f,0.65f};   // mp5
    case 30:return {190,75,0.05f,0.016f,0.40f,0.85f};    // ak47: chunky
    case 31:return {230,95,0.04f,0.013f,0.35f,0.75f};    // m4: tighter
    case 33:return {210,55,0.13f,0.040f,0.35f,1.00f};    // country rifle
    case 34:return {190,40,0.20f,0.065f,0.40f,1.00f};    // sniper: big slow thump
    case 35:case 36:return {100,30,0.45f,0.150f,0.60f,1.00f};   // rocket launchers: whoosh-thump
    case 38:return {200,120,0.02f,0.006f,0.40f,0.50f};   // minigun bullet (plus the spin loop)
    case 40:return {300,200,0.02f,0.006f,0.20f,0.40f};   // detonator click
    case 43:return {420,380,0.015f,0.004f,0.10f,0.35f};  // camera shutter
    default:return {0,0,0,0,0,0};
    }
}
inline Hit melee_hit(){return {160,60,0.07f,0.022f,0.50f,0.80f};}
inline Hit reload_click(bool insert){return insert?Hit{300,200,0.025f,0.008f,0.15f,0.40f}:Hit{380,300,0.02f,0.006f,0.20f,0.35f};}
inline Hit damage_hit(float k){return {120,50,0.12f,0.040f,0.60f,std::clamp(k,0.f,1.f)};}
inline Hit explosion_hit(float k){return {75,25,0.60f,0.220f,0.70f,std::clamp(k,0.f,1.f)};}
inline Hit crash_hit(float k){return {95,35,0.25f,0.090f,0.70f,std::clamp(k,0.f,1.f)};}
inline Hit landing_hit(float k){return {80,35,0.18f,0.060f,0.50f,std::clamp(k,0.f,1.f)};}
inline Hit bump_hit(float k){return {110,60,0.06f,0.020f,0.60f,std::clamp(k,0.f,1.f)*0.5f};}

// Footsteps by the surface's audio type (SurfaceInfos flags, see dualsense.cpp).
enum Surface:uint8_t{S_DEFAULT,S_CONCRETE,S_GRASS,S_SAND,S_GRAVEL,S_WOOD,S_WATER,S_METAL,S_TILE,S_COUNT};
inline const char* surface_name(Surface s){static const char* n[S_COUNT]={"default","concrete","grass","sand","gravel","wood","water","metal","tile"};return n[s<S_COUNT?s:0];}
inline Hit footstep_hit(Surface s){
    switch(s){
    case S_GRASS:return {130,80,0.045f,0.015f,0.75f,0.22f};   // soft, brushy
    case S_SAND:return {95,60,0.06f,0.025f,0.85f,0.25f};      // mushy
    case S_GRAVEL:return {210,120,0.04f,0.012f,0.95f,0.32f};  // gritty crunch
    case S_WOOD:return {170,110,0.05f,0.020f,0.15f,0.40f};    // hollow knock
    case S_WATER:return {80,50,0.09f,0.040f,0.90f,0.30f};     // splash
    case S_METAL:return {430,400,0.08f,0.035f,0.05f,0.30f};   // ring
    case S_TILE:return {240,140,0.03f,0.008f,0.30f,0.35f};    // hard click
    default:return {190,110,0.03f,0.008f,0.35f,0.35f};        // concrete / asphalt
    }
}
// Steps per second for a walking speed in m/s (walk ~1.9, run ~2.8, sprint ~3.4).
inline float step_rate(float speed){return std::clamp(1.3f+0.33f*speed,1.6f,3.6f);}

// ---------- recipes: several timed hits played as one effect ----------
struct Part{Hit h;float delay,gain,pan;};
struct Recipe{Part p[20];int n=0;void add(const Hit& h,float delay,float gain,float pan){if(n<20&&h.gain>0)p[n++]={h,delay,gain,pan};}};
inline float rnd(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return static_cast<float>(s&0xFFFFFF)/16777216.f;}   // 0..1
// Footsteps after Astro Bot's approach (feel tied to the sound of the surface, contrast over constant buzz):
// a heel strike plus a softer toe roll ~70 ms later; loose ground adds a cloud of grains (sand: many soft and spread out,
// gravel: fewer and sharper, grass: brushy); metal clicks and rings, wood knocks hollow. Hard ground stays subtle.
inline Recipe footstep(Surface s,float gain,float pan,uint32_t& rng){
    Recipe r;
    auto grains=[&](Hit g,int count,float start,float spread){for(int i=0;i<count;++i)r.add(g,start+rnd(rng)*spread,gain*(0.6f+0.8f*rnd(rng)),pan*(0.5f+0.5f*rnd(rng)));};
    switch(s){
    case S_METAL:r.add({650,380,0.008f,0.002f,0.5f,0.35f},0,gain,pan);r.add({430,425,0.14f,0.050f,0.02f,0.22f},0.002f,gain,pan);r.add({600,400,0.006f,0.002f,0.5f,0.15f},0.07f,gain,pan);break;
    case S_WOOD:r.add({180,120,0.05f,0.018f,0.10f,0.38f},0,gain,pan);r.add({360,340,0.03f,0.010f,0.05f,0.12f},0.003f,gain,pan);r.add({220,160,0.03f,0.010f,0.10f,0.15f},0.07f,gain,pan);break;
    case S_SAND:r.add({85,60,0.06f,0.025f,0.75f,0.30f},0,gain,pan);grains({120,90,0.018f,0.007f,0.90f,0.16f},12,0.005f,0.10f);break;   // v2.1 was too faint to feel
    case S_GRAVEL:r.add({150,90,0.03f,0.010f,0.60f,0.20f},0,gain,pan);grains({260,180,0.008f,0.003f,0.90f,0.14f},7,0.004f,0.05f);break;
    case S_GRASS:r.add({115,80,0.045f,0.016f,0.70f,0.22f},0,gain,pan);grains({150,100,0.02f,0.008f,0.90f,0.11f},7,0.01f,0.06f);break;
    case S_WATER:r.add({70,50,0.07f,0.030f,0.90f,0.18f},0,gain,pan);grains({90,70,0.03f,0.012f,0.90f,0.08f},8,0.02f,0.12f);break;
    case S_TILE:r.add({260,160,0.02f,0.005f,0.25f,0.33f},0,gain,pan);r.add({300,200,0.012f,0.004f,0.25f,0.14f},0.07f,gain,pan);break;
    default:r.add({170,110,0.025f,0.007f,0.30f,0.30f},0,gain,pan);r.add({210,150,0.015f,0.005f,0.30f,0.13f},0.07f,gain,pan);break;   // concrete / asphalt
    }
    return r;
}
// Molotov: the bottle cracks, glass tinkles scatter, then the fire catches with a soft whoomp.
inline Recipe glass_break(float k,float pan,uint32_t& rng){
    Recipe r;r.add({900,600,0.005f,0.0018f,0.5f,0.80f},0,k,pan);r.add({420,300,0.02f,0.006f,0.6f,0.50f},0.004f,k,pan);
    for(int i=0;i<10;++i)r.add({650+350*rnd(rng),450+250*rnd(rng),0.008f,0.003f,0.7f,0.45f},0.01f+0.10f*rnd(rng),k*(0.5f+0.7f*rnd(rng)),std::clamp(pan+(rnd(rng)-0.5f)*0.6f,-1.f,1.f));
    r.add({60,40,0.35f,0.14f,0.8f,0.80f},0.03f,k,pan);return r;
}
// Throwing a grenade or molotov: a quick swish as it leaves the hand.
inline Hit throw_swish(){return {130,60,0.12f,0.040f,0.9f,0.40f};}
// Bolt-action cycle: the bolt opens (two clacks) and later closes (one firm clack).
inline Recipe bolt_open(){Recipe r;r.add({520,300,0.012f,0.004f,0.4f,0.45f},0,1,0.35f);r.add({240,160,0.03f,0.010f,0.3f,0.30f},0.05f,1,0.35f);return r;}
inline Recipe bolt_close(){Recipe r;r.add({600,350,0.010f,0.003f,0.4f,0.55f},0,1,0.35f);r.add({200,140,0.02f,0.008f,0.2f,0.25f},0.01f,1,0.35f);return r;}
// Crashes by zone: front = sharp crunch, rear = dull push, side = hit plus a short scrape on that hand.
enum Zone:uint8_t{Z_FRONT,Z_REAR,Z_SIDE};
inline Recipe crash(float k,Zone z,float pan){
    Recipe r;k=std::clamp(k,0.f,1.f);
    if(z==Z_FRONT){r.add({140,45,0.18f,0.060f,0.6f,1.f},0,k,pan);r.add({300,200,0.02f,0.006f,0.8f,0.4f},0,k,pan);}
    else if(z==Z_REAR){r.add({80,30,0.28f,0.100f,0.7f,1.f},0,k,pan);}
    else{r.add({110,40,0.20f,0.070f,0.7f,1.f},0,k,pan);for(int i=0;i<4;++i)r.add({220,150,0.03f,0.010f,0.9f,0.25f},0.03f+0.04f*i,k,pan);}
    return r;
}

// Continuous layers, all 0..1 amplitudes (frequencies in Hz).
struct Layers{
    float engineHz=0,engineAmp=0;   // vehicle engine, pitch follows revs
    float terrainAmp=0,terrainGrit=0;   // road texture; grit 0 = smooth rumble, 1 = gravel
    float skidAmp=0;                // tyre screech / wheelspin
    float loopHz=0,loopAmp=0,loopNoise=0;   // minigun spin, flamethrower, chainsaw, sprays
    float rumbleLow=0,rumbleHigh=0; // the game's XInput rumble, converted
};

// ---------- sound files -> haptic clips ----------
// A clip is a mono haptic waveform at CLIP_RATE: the low band of a sound (what the voice coils can reproduce),
// trimmed to its attack and body, with the reverb tail faded out.
constexpr float CLIP_RATE=8000.f;
struct Clip{std::vector<float> s;};
// Minimal RIFF/WAVE reader: PCM 16/24/32-bit or float 32, any channel count, mixed to mono. false if not understood.
inline bool decode_wav(const uint8_t* d,size_t n,std::vector<float>& mono,float& rate){
    auto u16=[&](size_t o){return static_cast<unsigned>(d[o]|(d[o+1]<<8));};
    auto u32=[&](size_t o){return static_cast<uint32_t>(d[o]|(d[o+1]<<8)|(d[o+2]<<16)|(static_cast<uint32_t>(d[o+3])<<24));};
    if(n<12||std::memcmp(d,"RIFF",4)||std::memcmp(d+8,"WAVE",4))return false;
    unsigned fmt=0,ch=0,bits=0;uint32_t sr=0;size_t data=0,len=0;
    for(size_t o=12;o+8<=n;){
        uint32_t sz=u32(o+4);size_t body=o+8;if(sz>n-body)sz=static_cast<uint32_t>(n-body);
        if(!std::memcmp(d+o,"fmt ",4)&&sz>=16){fmt=u16(body);ch=u16(body+2);sr=u32(body+4);bits=u16(body+14);
            if(fmt==0xFFFE&&sz>=26)fmt=u16(body+24);}   // WAVE_FORMAT_EXTENSIBLE: the sub-format's first word
        else if(!std::memcmp(d+o,"data",4)){data=body;len=sz;}
        o=body+sz+(sz&1);
    }
    bool pcm=fmt==1&&(bits==16||bits==24||bits==32),flt=fmt==3&&bits==32;
    if(!data||!ch||ch>8||!sr||(!pcm&&!flt))return false;
    size_t frame=ch*bits/8,frames=len/frame;mono.assign(frames,0.f);rate=static_cast<float>(sr);
    for(size_t i=0;i<frames;++i){float sum=0;
        for(unsigned c=0;c<ch;++c){const uint8_t* p=d+data+i*frame+c*bits/8;float v;
            if(flt){float f;std::memcpy(&f,p,4);v=f;}
            else if(bits==16)v=static_cast<int16_t>(p[0]|(p[1]<<8))/32768.f;
            else if(bits==24)v=static_cast<float>(static_cast<int32_t>((static_cast<uint32_t>(p[0])<<8)|(static_cast<uint32_t>(p[1])<<16)|(static_cast<uint32_t>(p[2])<<24))>>8)/8388608.f;
            else v=static_cast<float>(static_cast<int32_t>(u32(static_cast<size_t>(p-d))))/2147483648.f;
            sum+=v;}
        mono[i]=sum/static_cast<float>(ch);}
    return frames>0;
}
// Sound -> haptic: band 30-300 Hz (two-pole filters), start at the attack, keep maxSeconds with the last 40 % faded,
// peak-normalised, then resampled to CLIP_RATE. Empty clip if the sound is silent.
inline Clip make_clip(const std::vector<float>& in,float rate,float maxSeconds=0.25f){
    Clip c;if(in.empty()||rate<1000)return c;
    std::vector<float> x(in);
    auto lowpass=[&](float fc){float k=1-std::exp(-TAU*fc/rate),a=0,b=0;for(float& v:x){a+=(v-a)*k;b+=(a-b)*k;v=b;}};
    auto highpass=[&](float fc){float k=1-std::exp(-TAU*fc/rate),lp=0;for(float& v:x){lp+=(v-lp)*k;v-=lp;}};
    lowpass(300);highpass(30);
    float peak=0;for(float v:x)peak=std::max(peak,std::fabs(v));if(peak<1e-4f)return c;
    size_t start=0;while(start<x.size()&&std::fabs(x[start])<peak*0.2f)++start;   // the body, not the quiet build-up before it
    start=start>static_cast<size_t>(rate*0.002f)?start-static_cast<size_t>(rate*0.002f):0;
    size_t len=std::min(x.size()-start,static_cast<size_t>(rate*maxSeconds));
    size_t outLen=static_cast<size_t>(static_cast<float>(len)*CLIP_RATE/rate);if(outLen<8)return c;
    c.s.resize(outLen);float step=rate/CLIP_RATE,top=0;
    for(size_t i=0;i<outLen;++i){float pos=static_cast<float>(i)*step;size_t j=start+static_cast<size_t>(pos);float f=pos-std::floor(pos);
        float v=x[j]+(j+1<start+len?(x[j+1]-x[j])*f:0.f);c.s[i]=v;top=std::max(top,std::fabs(v));}
    size_t fade=outLen*6/10;
    for(size_t i=0;i<outLen;++i){float g=1.f/top;if(i>fade)g*=std::exp(-4.f*static_cast<float>(i-fade)/static_cast<float>(outLen-fade));c.s[i]*=g;}
    return c;
}

// ---------- synthesizer ----------
class Synth{
public:
    explicit Synth(float rate=48000.f):rate_(rate){}
    // pan: -1 = left hand only, 0 = both, +1 = right hand only.
    // delay (s): the hit starts later, so a whole recipe (heel + toe, grain clouds) can be queued at once.
    void trigger(const Hit& h,float gain,float pan,float delay=0.f){
        if(h.gain<=0||gain<=0||h.dur<=0)return;
        int slot=0;float weakest=1e9f;
        for(int i=0;i<VOICES;++i){if(!v_[i].on){slot=i;break;}float lvl=v_[i].level();if(lvl<weakest){weakest=lvl;slot=i;}}
        Voice& v=v_[slot];v=Voice{};v.on=true;v.h=h;v.amp=std::clamp(h.gain*gain,0.f,1.5f);v.t=-std::max(0.f,delay);
        pan=std::clamp(pan,-1.f,1.f);v.l=pan<=0?1.f:1.f-pan;v.r=pan>=0?1.f:1.f+pan;v.seed=seed_+=0x9E3779B9u;
    }
    // Plays a haptic clip (made by make_clip); the clip must outlive the voice (they are loaded once and kept).
    void trigger(const Clip* c,float gain,float pan,float delay=0.f){
        if(!c||c->s.empty()||gain<=0)return;
        int slot=0;for(int i=0;i<CLIPS;++i)if(!c_[i].clip||c_[i].pos>c_[slot].pos){slot=i;if(!c_[i].clip)break;}   // free, else the oldest
        ClipVoice& v=c_[slot];v.clip=c;v.pos=-std::max(0.f,delay)*CLIP_RATE;v.amp=std::clamp(gain,0.f,1.5f);
        pan=std::clamp(pan,-1.f,1.f);v.l=pan<=0?1.f:1.f-pan;v.r=pan>=0?1.f:1.f+pan;
    }
    void set(const Layers& target){target_=target;}
    void silence(){for(Voice& v:v_)v.on=false;for(ClipVoice& v:c_)v.clip=nullptr;target_=Layers{};cur_=Layers{};}
    int active()const{int n=0;for(const Voice& v:v_)n+=v.on;for(const ClipVoice& v:c_)n+=v.clip!=nullptr;return n;}
    // Renders n frames into left/right actuator buffers (overwrites them).
    void render(float* L,float* R,int n){
        const float dt=1.f/rate_,smooth=1.f-std::exp(-dt/0.03f);
        for(int i=0;i<n;++i){
            // continuous layers glide towards their targets (30 ms) so parameter steps never click
            lerpTo(cur_.engineHz,target_.engineHz,smooth);lerpTo(cur_.engineAmp,target_.engineAmp,smooth);
            lerpTo(cur_.terrainAmp,target_.terrainAmp,smooth);lerpTo(cur_.terrainGrit,target_.terrainGrit,smooth);
            lerpTo(cur_.skidAmp,target_.skidAmp,smooth);lerpTo(cur_.loopHz,target_.loopHz,smooth);lerpTo(cur_.loopAmp,target_.loopAmp,smooth);
            lerpTo(cur_.loopNoise,target_.loopNoise,smooth);lerpTo(cur_.rumbleLow,target_.rumbleLow,smooth);lerpTo(cur_.rumbleHigh,target_.rumbleHigh,smooth);
            float n0=noise(seed_);
            float c=0;
            if(cur_.engineAmp>1e-4f){phEngine_=wrap(phEngine_+TAU*cur_.engineHz*dt);
                c+=cur_.engineAmp*(std::sin(phEngine_)*(0.7f+0.3f*std::sin(phEngine_*0.5f))+0.3f*std::sin(2*phEngine_));}   // firing pulses
            if(cur_.terrainAmp>1e-4f){float fc=60+260*cur_.terrainGrit;terrain_+=(n0-terrain_)*(1-std::exp(-TAU*fc*dt));c+=cur_.terrainAmp*terrain_*3.f;}
            if(cur_.skidAmp>1e-4f){skidLo_+=(n0-skidLo_)*(1-std::exp(-TAU*420*dt));skidLo2_+=(skidLo_-skidLo2_)*(1-std::exp(-TAU*140*dt));c+=cur_.skidAmp*(skidLo_-skidLo2_)*4.f;}
            if(cur_.loopAmp>1e-4f){phLoop_=wrap(phLoop_+TAU*cur_.loopHz*dt);loop_+=(n0-loop_)*(1-std::exp(-TAU*(cur_.loopHz*3+80)*dt));
                c+=cur_.loopAmp*((1-cur_.loopNoise)*std::sin(phLoop_)+cur_.loopNoise*loop_*2.5f);}
            if(cur_.rumbleLow>1e-4f){phLow_=wrap(phLow_+TAU*45*dt);c+=cur_.rumbleLow*std::sin(phLow_);}
            if(cur_.rumbleHigh>1e-4f){phHigh_=wrap(phHigh_+TAU*150*dt);c+=cur_.rumbleHigh*0.6f*std::sin(phHigh_);}
            float l=c,r=c;
            for(Voice& v:v_){if(!v.on)continue;float s=v.sample(dt);l+=s*v.l;r+=s*v.r;}
            for(ClipVoice& v:c_){if(!v.clip)continue;
                if(v.pos>=0){size_t j=static_cast<size_t>(v.pos);const std::vector<float>& s=v.clip->s;
                    if(j+1>=s.size()){v.clip=nullptr;continue;}
                    float f=v.pos-static_cast<float>(j),x=(s[j]+(s[j+1]-s[j])*f)*v.amp;l+=x*v.l;r+=x*v.r;}
                v.pos+=CLIP_RATE*dt;}
            L[i]=clip(l);R[i]=clip(r);
        }
    }
private:
    struct Voice{
        bool on=false;Hit h{};float t=0,ph=0,l=1,r=1,amp=1,lp=0;uint32_t seed=1;
        float env()const{const float attack=0.002f;return t<0?0.f:t<attack?t/attack:std::exp(-(t-attack)/h.decay);}
        float level()const{return !on?0.f:t<0?amp:amp*env();}   // a pending (delayed) hit counts as loud, so it is not stolen
        float sample(float dt){
            if(t<0){t+=dt;return 0.f;}
            float f=h.f1+(h.f0-h.f1)*std::exp(-t*6.f/h.dur);
            ph=wrap(ph+TAU*f*dt);
            float n=noise(seed);lp+=(n-lp)*(1-std::exp(-TAU*f*2.f*dt));
            float s=((1-h.noise)*std::sin(ph)+h.noise*lp*2.f)*env()*amp;
            t+=dt;if(t>std::max(h.dur,h.decay*6.f))on=false;
            return s;
        }
    };
    struct ClipVoice{const Clip* clip=nullptr;float pos=0,amp=1,l=1,r=1;};
    static constexpr int VOICES=48;   // grain clouds need room
    static constexpr int CLIPS=8;     // automatics overlap a few shots
    static float noise(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return static_cast<float>(static_cast<int32_t>(s))*(1.f/2147483648.f);}
    static float wrap(float p){return p>TAU*64?p-TAU*64:p;}
    static void lerpTo(float& c,float t,float k){c+=(t-c)*k;}
    static float clip(float x){x=std::clamp(x,-3.f,3.f);return std::clamp(x*(27+x*x)/(27+9*x*x),-1.f,1.f);}   // soft limiter (tanh-like)
    Voice v_[VOICES];ClipVoice c_[CLIPS];Layers target_,cur_;float rate_;uint32_t seed_=0x12345678u;
    float phEngine_=0,phLoop_=0,phLow_=0,phHigh_=0,terrain_=0,skidLo_=0,skidLo2_=0,loop_=0;
};

// Engine pitch and strength for a vehicle (revs 0..1 from m_fGasPedalAudioRevs). heavy = big trucks/buses.
inline void engine_layer(int kind,float revs,float gas,bool heavy,Layers& out){
    revs=std::clamp(revs,0.f,1.f);gas=std::clamp(std::fabs(gas),0.f,1.f);
    float base=heavy?24.f:kind==2?40.f:32.f,span=heavy?50.f:kind==2?95.f:70.f;   // kind 2 = bike: higher, buzzier
    out.engineHz=base+revs*span;out.engineAmp=(heavy?0.12f:0.08f)+0.20f*revs+0.08f*gas;
}

} // namespace hx
