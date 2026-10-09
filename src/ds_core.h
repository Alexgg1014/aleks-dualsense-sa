#pragma once
// Alek's DualSense SA: pure logic (no Windows, no game memory) so test_ds.cpp can check it offline.
// DualSense USB protocol: input report 0x01 (64 bytes), output report 0x02 (48 bytes).
// Byte layouts follow the Linux hid-playstation driver and SDL's HIDAPI PS5 driver; trigger effect encodings follow
// Sony's "official" modes (0x21 feedback, 0x22 bow, 0x23 galloping, 0x25 weapon, 0x26 vibration, 0x27 machine).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ds {

// ---------- input ----------
struct Input {
    uint8_t lx=128,ly=128,rx=128,ry=128,l2=0,r2=0,hat=8,counter=0,battery=0;
    bool square=false,cross=false,circle=false,triangle=false,l1=false,r1=false,l2d=false,r2d=false;
    bool create=false,options=false,l3=false,r3=false,ps=false,touch=false,mute=false,charging=false;
};
inline bool parse_usb_input(const uint8_t* r,size_t n,Input& o){
    if(n<64||r[0]!=0x01)return false;
    o.lx=r[1];o.ly=r[2];o.rx=r[3];o.ry=r[4];o.l2=r[5];o.r2=r[6];o.counter=r[7];
    uint8_t b0=r[8],b1=r[9],b2=r[10];
    o.hat=b0&0x0F;o.square=b0&0x10;o.cross=b0&0x20;o.circle=b0&0x40;o.triangle=b0&0x80;
    o.l1=b1&0x01;o.r1=b1&0x02;o.l2d=b1&0x04;o.r2d=b1&0x08;o.create=b1&0x10;o.options=b1&0x20;o.l3=b1&0x40;o.r3=b1&0x80;
    o.ps=b2&0x01;o.touch=b2&0x02;o.mute=b2&0x04;
    o.battery=static_cast<uint8_t>(std::min(10,r[53]&0x0F)*10);o.charging=((r[53]>>4)&0x0F)==1;
    return true;
}

// XInput view of the pad (same bit values as XINPUT_GAMEPAD_*).
struct XPad{uint16_t buttons=0;uint8_t lt=0,rt=0;int16_t lx=0,ly=0,rx=0,ry=0;};
constexpr uint16_t X_UP=0x0001,X_DOWN=0x0002,X_LEFT=0x0004,X_RIGHT=0x0008,X_START=0x0010,X_BACK=0x0020,X_LTHUMB=0x0040,X_RTHUMB=0x0080,
    X_LB=0x0100,X_RB=0x0200,X_GUIDE=0x0400,X_A=0x1000,X_B=0x2000,X_X=0x4000,X_Y=0x8000;
// 0..255 -> -32768..32767. DualSense Y grows downwards, XInput Y grows upwards.
inline int16_t axis(uint8_t v,bool invert){
    int a=static_cast<int>(v)*257-32768;if(invert)a=-a-1;
    return static_cast<int16_t>(std::clamp(a,-32768,32767));
}
// Radial deadzone in percent of the stick range (0 = raw, GInput applies its own).
inline void deadzone(int16_t& x,int16_t& y,int percent){
    if(percent<=0)return;
    float fx=x/32767.f,fy=y/32767.f,m=std::sqrt(fx*fx+fy*fy),dz=percent/100.f;
    if(m<=dz){x=y=0;return;}
    float s=std::min(1.f,(m-dz)/(1.f-dz))/m;
    x=static_cast<int16_t>(std::clamp(fx*s*32767.f,-32768.f,32767.f));y=static_cast<int16_t>(std::clamp(fy*s*32767.f,-32768.f,32767.f));
}
inline XPad to_xinput(const Input& i,bool touchpadAsBack,int deadzonePercent){
    XPad p;
    static const uint16_t hats[9]={X_UP,X_UP|X_RIGHT,X_RIGHT,X_DOWN|X_RIGHT,X_DOWN,X_DOWN|X_LEFT,X_LEFT,X_UP|X_LEFT,0};
    p.buttons=hats[i.hat<=8?i.hat:8];
    if(i.cross)p.buttons|=X_A;if(i.circle)p.buttons|=X_B;if(i.square)p.buttons|=X_X;if(i.triangle)p.buttons|=X_Y;
    if(i.l1)p.buttons|=X_LB;if(i.r1)p.buttons|=X_RB;if(i.l3)p.buttons|=X_LTHUMB;if(i.r3)p.buttons|=X_RTHUMB;
    if(i.options)p.buttons|=X_START;if(i.create||(touchpadAsBack&&i.touch))p.buttons|=X_BACK;if(i.ps)p.buttons|=X_GUIDE;
    p.lt=i.l2;p.rt=i.r2;
    p.lx=axis(i.lx,false);p.ly=axis(i.ly,true);p.rx=axis(i.rx,false);p.ry=axis(i.ry,true);
    deadzone(p.lx,p.ly,deadzonePercent);deadzone(p.rx,p.ry,deadzonePercent);
    return p;
}
// "Fire at the click": below the weapon's break point the game sees an untouched trigger, at the break it sees full.
inline uint8_t remap_fire(uint8_t raw,int firePoint){if(firePoint<=0)return raw;return raw>=firePoint?255:0;}

// ---------- adaptive trigger effects ----------
struct TriggerFx{
    uint8_t b[11]{};
    bool operator==(const TriggerFx& o)const{return std::memcmp(b,o.b,sizeof b)==0;}
    bool operator!=(const TriggerFx& o)const{return !(*this==o);}
};
inline TriggerFx fx_off(){TriggerFx f;f.b[0]=0x05;return f;}
inline void put_zones(TriggerFx& f,uint32_t active,uint32_t values){
    f.b[1]=active&0xFF;f.b[2]=(active>>8)&0xFF;
    f.b[3]=values&0xFF;f.b[4]=(values>>8)&0xFF;f.b[5]=(values>>16)&0xFF;f.b[6]=(values>>24)&0xFF;
}
// Per-zone resistance, zones 0..9 along the trigger travel, strength 1..8 (0 = zone off).
inline TriggerFx fx_multi_feedback(const int s[10]){
    uint32_t active=0,force=0;
    for(int i=0;i<10;++i)if(s[i]>0){active|=1u<<i;force|=static_cast<uint32_t>((std::min(s[i],8)-1)&7)<<(3*i);}
    if(!active)return fx_off();
    TriggerFx f;f.b[0]=0x21;put_zones(f,active,force);return f;
}
// Constant resistance from a zone to the end of the travel.
inline TriggerFx fx_feedback(int pos,int strength){
    if(strength<=0)return fx_off();
    int s[10]{};for(int i=std::clamp(pos,0,9);i<10;++i)s[i]=strength;return fx_multi_feedback(s);
}
// Resistance that ramps from s0 at zone a to s1 at zone b and stays at s1 afterwards.
inline TriggerFx fx_slope(int a,int b,int s0,int s1){
    a=std::clamp(a,0,8);b=std::clamp(b,a+1,9);int s[10]{};
    for(int i=a;i<10;++i){float t=i>=b?1.f:static_cast<float>(i-a)/static_cast<float>(b-a);s[i]=static_cast<int>(std::lround(s0+(s1-s0)*t));}
    return fx_multi_feedback(s);
}
// Gun trigger: resistance between two zones, then it snaps (the "click").
inline TriggerFx fx_weapon(int start,int end,int strength){
    if(strength<=0)return fx_off();
    start=std::clamp(start,2,7);end=std::clamp(end,start+1,8);
    TriggerFx f;f.b[0]=0x25;uint32_t zones=(1u<<start)|(1u<<end);f.b[1]=zones&0xFF;f.b[2]=(zones>>8)&0xFF;
    f.b[3]=static_cast<uint8_t>((std::min(strength,8)-1)&7);return f;
}
// Vibration from a zone to the end of the travel; frequency in Hz.
inline TriggerFx fx_vibration(int pos,int amplitude,int freq){
    if(amplitude<=0||freq<=0)return fx_off();
    uint32_t active=0,amp=0;
    for(int i=std::clamp(pos,0,9);i<10;++i){active|=1u<<i;amp|=static_cast<uint32_t>((std::min(amplitude,8)-1)&7)<<(3*i);}
    TriggerFx f;f.b[0]=0x26;put_zones(f,active,amp);f.b[9]=static_cast<uint8_t>(std::clamp(freq,1,255));return f;
}
// Bow string: tension from start to end, then it snaps with snapForce.
inline TriggerFx fx_bow(int start,int end,int strength,int snap){
    if(strength<=0)return fx_off();
    start=std::clamp(start,0,8);end=std::clamp(end,start+1,8);
    TriggerFx f;f.b[0]=0x22;uint32_t zones=(1u<<start)|(1u<<end);f.b[1]=zones&0xFF;f.b[2]=(zones>>8)&0xFF;
    uint32_t pair=static_cast<uint32_t>((std::min(strength,8)-1)&7)|(static_cast<uint32_t>((std::clamp(snap,1,8)-1)&7)<<3);
    f.b[3]=pair&0xFF;f.b[4]=(pair>>8)&0xFF;return f;
}
// Galloping: two "feet" per cycle (used for BMX pedal strokes).
inline TriggerFx fx_galloping(int start,int end,int foot1,int foot2,int freq){
    start=std::clamp(start,0,8);end=std::clamp(end,start+1,9);foot1=std::clamp(foot1,0,6);foot2=std::clamp(foot2,foot1+1,7);
    TriggerFx f;f.b[0]=0x23;uint32_t zones=(1u<<start)|(1u<<end);f.b[1]=zones&0xFF;f.b[2]=(zones>>8)&0xFF;
    f.b[3]=static_cast<uint8_t>((foot2&7)|((foot1&7)<<3));f.b[4]=static_cast<uint8_t>(std::clamp(freq,1,255));return f;
}
// Machine: alternates two amplitudes; period in tenths of a second (used for a coughing engine).
inline TriggerFx fx_machine(int start,int end,int ampA,int ampB,int freq,int period){
    start=std::clamp(start,0,8);end=std::clamp(end,start+1,9);
    TriggerFx f;f.b[0]=0x27;uint32_t zones=(1u<<start)|(1u<<end);f.b[1]=zones&0xFF;f.b[2]=(zones>>8)&0xFF;
    f.b[3]=static_cast<uint8_t>((std::clamp(ampA,0,7)&7)|((std::clamp(ampB,0,7)&7)<<3));
    f.b[4]=static_cast<uint8_t>(std::clamp(freq,1,255));f.b[5]=static_cast<uint8_t>(std::clamp(period,0,255));return f;
}
// INI percentage on the 1..8 scale: 100% keeps the profile value, lower values shrink the range above the minimum.
inline int scale(int raw,int percent){
    if(raw<=0||percent<=0)return 0;
    return std::clamp(static_cast<int>(std::lround(1+(raw-1)*std::min(percent,150)/100.0)),1,8);
}
// Raw trigger value (0..255) where a zone begins.
inline int zone_raw(int zone){return static_cast<int>(std::lround(std::clamp(zone,0,10)*25.5));}

// ---------- weapons ----------
enum Kind:uint8_t{K_NONE,K_CLICK,K_HEAVY,K_TWOSTAGE,K_AUTO,K_MINIGUN,K_BOW,K_SPRAY,K_CHAINSAW,K_BOLT};
struct WeaponProfile{
    const char* name;Kind kind;
    uint8_t a,b,c,d;          // CLICK/HEAVY: start,end,strength. TWOSTAGE: light,wallZone,heavy. AUTO: pos,strength,amp,Hz.
    uint8_t aim;              // L2 resistance while holding the gun (1..8, 0 = none)
    uint8_t rumL,rumR;uint16_t rumMs;   // rumble kick per shot
};
// Indexed by eWeaponType (plugin-sdk enums/eWeaponType.h).
inline const WeaponProfile& weapon_profile(unsigned type){
    static const WeaponProfile none{"none",K_NONE,0,0,0,0,0,0,0,0};
    static const WeaponProfile melee{"melee",K_NONE,0,0,0,0,0,0,0,0};
    static const WeaponProfile table[]={
        melee,melee,melee,melee,melee,melee,melee,melee,melee,                     // 0..8 fists, knuckles, clubs, knife, bat, shovel, cue, katana
        {"chainsaw",K_CHAINSAW,1,2,6,38,0,90,150,0},                               // 9
        melee,melee,melee,melee,melee,melee,                                       // 10..15 toys, flowers, cane
        {"grenade",K_BOW,1,6,4,6,0,60,90,140},{"teargas",K_BOW,1,6,3,5,0,40,70,110},{"molotov",K_BOW,1,6,4,6,0,60,90,140}, // 16..18
        none,none,none,                                                            // 19..21 projectiles, not held
        {"pistol",K_CLICK,3,5,6,0,2,70,150,70},{"silenced",K_CLICK,3,5,5,0,2,45,110,60},{"deagle",K_CLICK,3,6,8,0,3,140,220,100}, // 22..24
        {"shotgun",K_HEAVY,2,6,8,0,3,230,255,150},{"sawnoff",K_HEAVY,2,5,8,0,3,255,255,160},{"spas12",K_HEAVY,2,5,6,0,3,200,240,120}, // 25..27
        {"uzi",K_AUTO,2,2,4,22,2,70,150,45},{"mp5",K_AUTO,2,3,5,16,2,80,160,50},  // 28..29
        {"ak47",K_AUTO,2,5,7,10,3,130,200,65},{"m4",K_AUTO,2,4,6,12,3,110,190,60}, // 30..31
        {"tec9",K_AUTO,2,2,4,24,2,70,150,45},                                      // 32
        {"rifle",K_BOLT,4,7,8,0,3,190,245,170},{"sniper",K_BOLT,4,7,8,0,3,220,255,190}, // 33..34
        {"rocket",K_HEAVY,2,6,8,0,4,255,255,320},{"heatseeker",K_HEAVY,2,6,8,0,4,255,255,320}, // 35..36
        {"flamethrower",K_SPRAY,1,3,4,28,2,60,110,0},{"minigun",K_MINIGUN,2,6,8,9,4,210,230,0}, // 37..38
        {"satchel",K_BOW,1,6,3,5,0,40,70,110},{"detonator",K_CLICK,2,4,3,0,0,60,100,60}, // 39..40
        {"spraycan",K_SPRAY,1,2,2,30,0,0,50,0},{"extinguisher",K_SPRAY,1,3,3,22,0,30,80,0},{"camera",K_CLICK,3,5,3,0,0,0,40,40}, // 41..43
        none,none,none                                                             // 44..46 goggles, parachute
    };
    return type<sizeof table/sizeof table[0]?table[type]:none;
}
struct WeaponState{unsigned type=0;bool hasAmmo=true,reloading=false,firing=false,cycling=false;   // cycling: bolt still working (game time < next shot)
   unsigned long long sinceShotMs=100000;};
struct TriggerPair{TriggerFx l2,r2;int firePoint=0;const char* name="off";};
inline TriggerPair weapon_triggers(const WeaponState& w,int percent,bool fireAtClick){
    const WeaponProfile& p=weapon_profile(w.type);TriggerPair t;t.l2=fx_off();t.r2=fx_off();t.name=p.name;
    if(p.kind==K_NONE||percent<=0)return t;
    if(p.aim)t.l2=fx_feedback(2,scale(p.aim,percent));
    if(!w.hasAmmo){t.name="empty";return t;}           // empty gun: loose trigger, raw input
    if(w.reloading){t.name="reloading";return t;}
    switch(p.kind){
    case K_CLICK:t.r2=fx_weapon(p.a,p.b,scale(p.c,percent));if(fireAtClick)t.firePoint=zone_raw(p.b)-6;break;
    case K_HEAVY:
        // Hard pull and a dry knock right after the shot.
        if(w.sinceShotMs<110){t.r2=fx_vibration(0,scale(8,percent),40);t.name="kick";}
        else t.r2=fx_weapon(p.a,p.b,scale(p.c,percent));
        if(fireAtClick)t.firePoint=zone_raw(p.b)-6;break;
    case K_BOLT:
        // COD-style bolt action: slack, a firm wall, a crisp break that fires; then the trigger goes dead while the bolt cycles.
        if(w.sinceShotMs<130){t.r2=fx_vibration(0,scale(8,std::min(100,percent+30)),30);t.name="recoil";}
        else if(w.cycling){t.name="bolt-cycling";}
        else t.r2=fx_weapon(p.a,p.b,scale(p.c,std::min(100,percent+30)));
        if(fireAtClick)t.firePoint=zone_raw(p.b)-6;break;
    case K_TWOSTAGE:{
        // First stage light (half press), then a wall that must be pushed through to fire.
        int s[10]{};int light=scale(p.a,percent),heavy=scale(p.c,std::min(100,percent+30));   // the wall stays near full force (felt weak at 70 %)
        for(int i=1;i<p.b;++i)s[i]=light;for(int i=p.b;i<10;++i)s[i]=heavy;
        if(w.sinceShotMs<170){t.r2=fx_vibration(0,scale(8,std::min(100,percent+30)),30);t.name="recoil";}else t.r2=fx_multi_feedback(s);
        if(fireAtClick)t.firePoint=zone_raw(p.b)+8;break;}
    case K_AUTO:
        if(w.firing||w.sinceShotMs<140){t.r2=fx_vibration(p.a,scale(p.c,percent),p.d);t.name="auto-fire";}
        else t.r2=fx_feedback(p.a,scale(p.b,percent));
        if(fireAtClick)t.firePoint=zone_raw(p.a)+6;break;
    case K_MINIGUN:
        if(w.firing||w.sinceShotMs<160){t.r2=fx_vibration(p.a,scale(p.c,percent),p.d);t.name="minigun-fire";}
        else t.r2=fx_feedback(p.a,scale(p.b,percent));
        if(fireAtClick)t.firePoint=zone_raw(p.a)+6;break;
    case K_BOW:t.r2=fx_bow(p.a,p.b,scale(p.c,percent),scale(p.d,percent));break;   // throws keep the raw trigger (hold = farther)
    case K_SPRAY:
        if(w.firing){t.r2=fx_vibration(p.a,scale(p.c,percent),p.d);t.name="spraying";}
        else t.r2=fx_feedback(p.a,scale(p.b,percent));break;
    case K_CHAINSAW:
        if(w.firing){t.r2=fx_vibration(p.a,scale(p.c,percent),p.d);t.name="chainsaw-cut";}
        else t.r2=fx_vibration(p.a,scale(p.b,percent),18);break;   // idling engine
    default:break;
    }
    return t;
}

// ---------- vehicles ----------
enum VehicleKind:uint8_t{V_NONE,V_CAR,V_BIKE,V_BMX,V_BOAT,V_AIR,V_TRAIN};
// eVehicleType (plugin-sdk enums/eVehicleType.h): 0 car, 1 monster truck, 2 quad, 3 heli, 4 plane, 5 boat, 6 train,
// 7 RC heli, 8 RC plane, 9 bike, 10 BMX, 11 trailer.
inline VehicleKind vehicle_kind(int subClass){
    switch(subClass){case 0:case 1:return V_CAR;case 2:case 9:return V_BIKE;case 10:return V_BMX;case 5:return V_BOAT;
        case 3:case 4:case 7:case 8:return V_AIR;case 6:return V_TRAIN;default:return V_NONE;}
}
struct VehicleState{VehicleKind kind=V_NONE;float kmh=0,health=1000;bool braking=false,skidding=false,spinning=false,handbrake=false,inAir=false;};
inline TriggerPair vehicle_triggers(const VehicleState& v,int percent,bool speedResponse,float maxKmh=160){
    TriggerPair t;t.l2=fx_off();t.r2=fx_off();
    if(percent<=0||v.kind==V_NONE||v.kind==V_TRAIN){t.name="off";return t;}
    float sp=speedResponse?std::clamp(v.kmh/maxKmh,0.f,1.f):0.5f;
    switch(v.kind){
    case V_AIR:   // constant tension on both
        t.r2=fx_feedback(1,scale(5,percent));t.l2=fx_feedback(1,scale(5,percent));t.name="air";return t;
    case V_BMX:{  // pedal strokes that speed up with the bike
        int hz=std::clamp(static_cast<int>(std::lround(1+v.kmh/9.f)),1,7);
        t.r2=v.kmh>3?fx_galloping(1,9,2,4,hz):fx_feedback(1,scale(3,percent));t.l2=fx_feedback(2,scale(4,percent));t.name="bmx";return t;}
    default:break;
    }
    float soft=v.kind==V_BIKE?0.6f:v.kind==V_BOAT?0.7f:1.f;
    int lo=scale(static_cast<int>(std::lround((1+2*sp)*soft)),percent),hi=scale(static_cast<int>(std::lround((3+5*sp)*soft)),percent);
    t.r2=fx_slope(1,9,std::max(1,lo),std::max(1,hi));t.name=v.kind==V_BIKE?"bike":v.kind==V_BOAT?"boat":"car";
    if(v.kind!=V_BOAT&&v.spinning&&!v.inAir){t.r2=fx_vibration(1,scale(4,percent),26);t.name="wheelspin";}
    else if(v.health<300&&v.kind!=V_BOAT){t.r2=fx_machine(1,9,1,5,7,2);t.name="engine-dying";}   // period 0-2 (Nielk1 / DSX)
    t.l2=fx_feedback(2,scale(static_cast<int>(std::lround(7*soft)),percent));
    if(v.kmh>12&&!v.inAir&&(v.handbrake||(v.braking&&v.skidding))){t.l2=fx_vibration(2,scale(6,percent),v.handbrake?9:14);t.name=v.handbrake?"handbrake":"abs";}
    return t;
}

// ---------- rumble ----------
struct Pulse{float l=0,r=0;unsigned long long start=0;unsigned dur=0;};
struct Rumble{
    Pulse p[12];
    void add(float l,float r,unsigned dur,unsigned long long now){
        int slot=0;float weakest=1e9f;
        for(int i=0;i<12;++i){float rest=level_of(p[i],now);if(rest<weakest){weakest=rest;slot=i;}}
        p[slot]={std::clamp(l,0.f,255.f),std::clamp(r,0.f,255.f),now,std::max(dur,1u)};
    }
    static float level_of(const Pulse& q,unsigned long long now){
        if(!q.dur||now<q.start||now-q.start>=q.dur)return 0;return std::max(q.l,q.r)*(1.f-static_cast<float>(now-q.start)/q.dur);}
    void level(unsigned long long now,float& l,float& r)const{
        l=r=0;
        for(const Pulse& q:p){if(!q.dur||now<q.start||now-q.start>=q.dur)continue;float k=1.f-static_cast<float>(now-q.start)/q.dur;l=std::max(l,q.l*k);r=std::max(r,q.r*k);}
    }
    void clear(){for(Pulse& q:p)q=Pulse{};}
};

// ---------- lights ----------
struct Rgb{uint8_t r=0,g=0,b=0;};
inline uint8_t lerp8(uint8_t a,uint8_t b,float t){return static_cast<uint8_t>(std::lround(a+(b-a)*std::clamp(t,0.f,1.f)));}
inline Rgb lerp(Rgb a,Rgb b,float t){return {lerp8(a.r,b.r,t),lerp8(a.g,b.g,t),lerp8(a.b,b.b,t)};}
inline Rgb dim(Rgb c,float k){k=std::clamp(k,0.f,1.f);return {static_cast<uint8_t>(std::lround(c.r*k)),static_cast<uint8_t>(std::lround(c.g*k)),static_cast<uint8_t>(std::lround(c.b*k))};}
// Green when healthy, yellow at half, red when low; under 20% it beats like a heart.
inline Rgb health_color(float frac,unsigned long long now){
    frac=std::clamp(frac,0.f,1.f);const Rgb red{255,0,0},yellow{255,170,0},green{0,255,30};
    Rgb c=frac>=0.5f?lerp(yellow,green,(frac-0.5f)/0.5f):lerp(red,yellow,frac/0.5f);
    if(frac<0.2f){unsigned ph=static_cast<unsigned>(now%900);float beat=ph<120?1.f:ph<240?0.45f:ph<360?0.9f:0.25f;c=dim(c,beat);}
    return c;
}
// Five player LEDs filled left to right (bit 0 = leftmost).
inline uint8_t gauge_leds(float frac){
    if(frac<=0.f)return 0;int n=std::clamp(static_cast<int>(std::ceil(frac*5.f-1e-4f)),1,5);return static_cast<uint8_t>((1u<<n)-1);
}
inline Rgb speed_color(float frac){
    frac=std::clamp(frac,0.f,1.f);const Rgb green{0,255,40},yellow{255,200,0},red{255,0,0};
    return frac<0.6f?lerp(green,yellow,frac/0.6f):lerp(yellow,red,(frac-0.6f)/0.4f);
}
// Red/blue police flashes, faster with more stars. Returns false when no flash applies.
inline bool police_flash(unsigned wanted,unsigned long long now,Rgb& out){
    if(!wanted)return false;unsigned period=std::max(260u,1000u-std::min(wanted,6u)*120u);unsigned ph=static_cast<unsigned>(now%period);
    out=ph<period/2?Rgb{255,0,0}:Rgb{0,40,255};
    if((ph%(period/2))>period/2-period/10)out=Rgb{0,0,0};   // short gap between colours
    return true;
}

// ---------- output report ----------
struct OutState{
    uint8_t rumL=0,rumR=0;TriggerFx l2=fx_off(),r2=fx_off();Rgb light{0,80,200};uint8_t leds=0x04;bool micLed=false;
    bool hd=false;   // HD haptics: the actuators play the audio endpoint (channels 3-4) instead of emulated rumble
    bool operator==(const OutState& o)const{return hd==o.hd&&rumL==o.rumL&&rumR==o.rumR&&l2==o.l2&&r2==o.r2&&light.r==o.light.r&&light.g==o.light.g&&light.b==o.light.b&&leds==o.leds&&micLed==o.micLed;}
    bool operator!=(const OutState& o)const{return !(*this==o);}
};
constexpr size_t USB_OUTPUT_SIZE=48;
// setupLights: first report after connecting fades out the firmware's blue connection animation so the lightbar obeys.
// releaseLeds: last report on exit hands the LEDs back to the firmware.
inline void build_output(const OutState& s,bool vibrationV2,bool setupLights,bool releaseLeds,uint8_t out[USB_OUTPUT_SIZE]){
    std::memset(out,0,USB_OUTPUT_SIZE);
    out[0]=0x02;
    // Byte 1: 0x01 rumble emulation, 0x02 "use rumble instead of audio haptics", 0x04/0x08 right/left trigger effects.
    // HD haptics leave both rumble bits clear so the voice coils follow the audio channels 3-4.
    out[1]=0x04|0x08|(s.hd?0:(0x02|(vibrationV2?0:0x01)));
    out[2]=0x01|0x04|0x10|(releaseLeds?0x08:0);          // mic LED, lightbar, player LEDs
    out[3]=s.hd?0:s.rumR;out[4]=s.hd?0:s.rumL;              // weak (right) and strong (left) motor
    out[9]=s.micLed?1:0;
    std::memcpy(out+11,s.r2.b,11);std::memcpy(out+22,s.l2.b,11);
    out[39]=static_cast<uint8_t>((vibrationV2&&!s.hd?0x04:0)|(setupLights?0x02:0));
    out[42]=setupLights?0x02:0;                           // lightbar setup: light out
    out[43]=0;                                            // player LED brightness: high
    out[44]=s.leds&0x1F;
    out[45]=s.light.r;out[46]=s.light.g;out[47]=s.light.b;
}
inline const char* fx_name(const TriggerFx& f){
    switch(f.b[0]){case 0x05:case 0x00:return "off";case 0x21:return "feedback";case 0x22:return "bow";case 0x23:return "galloping";
        case 0x25:return "weapon";case 0x26:return "vibration";case 0x27:return "machine";default:return "?";}
}

} // namespace ds
