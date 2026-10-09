#include "ds_core.h"
#include "ds_haptics.h"
#include <cstdio>
#include <string_view>
static int failures=0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } }while(0)
using namespace ds;
static bool bytes(const TriggerFx& f,std::initializer_list<int> want){int i=0;for(int v:want){if(f.b[i]!=v)return false;++i;}for(;i<11;++i)if(f.b[i])return false;return true;}
int main(){
    // --- input report 0x01 (USB) ---
    uint8_t r[64]{};r[0]=0x01;r[1]=0;r[2]=255;r[3]=255;r[4]=0;r[5]=10;r[6]=200;
    r[8]=0x20|0x02;            // cross + hat east
    r[9]=0x01|0x20|0x80;       // L1, options, R3
    r[10]=0x02|0x01;           // touchpad, PS
    r[53]=0x18;                // charging, 80 %
    Input in;CHECK(parse_usb_input(r,64,in));
    CHECK(in.cross&&!in.circle&&in.hat==2&&in.l1&&in.options&&in.r3&&in.touch&&in.ps&&!in.mute);
    CHECK(in.l2==10&&in.r2==200&&in.battery==80&&in.charging);
    CHECK(!parse_usb_input(r,63,in));uint8_t bt[64]{};bt[0]=0x31;CHECK(!parse_usb_input(bt,64,in));
    parse_usb_input(r,64,in);
    XPad p=to_xinput(in,true,0);
    CHECK(p.buttons==(X_A|X_RIGHT|X_LB|X_START|X_RTHUMB|X_BACK|X_GUIDE));
    CHECK(p.lx==-32768&&p.ly==-32768&&p.rx==32767&&p.ry==32767);   // DualSense Y down = XInput Y up inverted
    CHECK(p.lt==10&&p.rt==200);
    XPad q=to_xinput(in,false,0);CHECK(!(q.buttons&X_BACK));
    // Hat directions.
    for(int h=0;h<9;++h){in.hat=static_cast<uint8_t>(h);XPad x=to_xinput(in,false,0);int dirs=x.buttons&0xF;
        static const int want[9]={X_UP,X_UP|X_RIGHT,X_RIGHT,X_DOWN|X_RIGHT,X_DOWN,X_DOWN|X_LEFT,X_LEFT,X_UP|X_LEFT,0};CHECK(dirs==want[h]);}
    // Axis edges and centre.
    CHECK(axis(0,false)==-32768&&axis(255,false)==32767&&axis(0,true)==32767&&axis(255,true)==-32768);
    CHECK(axis(128,false)>0&&axis(128,false)<300);
    int16_t dx=1000,dy=0;deadzone(dx,dy,10);CHECK(dx==0&&dy==0);dx=32767;dy=0;deadzone(dx,dy,10);CHECK(dx>32000);
    // Fire at the click.
    CHECK(remap_fire(100,0)==100&&remap_fire(100,130)==0&&remap_fire(130,130)==255&&remap_fire(255,130)==255);

    // --- DualShock 4 (USB) ---
    {uint8_t d[64]{};d[0]=0x01;d[1]=0;d[2]=255;d[3]=128;d[4]=128;d[5]=0x20|0x06;d[6]=0x10|0x02;d[7]=0x01|0x04;d[8]=40;d[9]=250;d[30]=0x10|0x07;
        Input in4;CHECK(parse_ds4_usb_input(d,64,in4));
        CHECK(in4.cross&&in4.hat==6&&in4.create&&in4.r1&&in4.ps&&!in4.mute&&in4.l2==40&&in4.r2==250&&in4.charging&&in4.battery==70);
        XPad p4=to_xinput(in4,true,0);CHECK((p4.buttons&(X_A|X_LEFT|X_BACK|X_RB))==(X_A|X_LEFT|X_BACK|X_RB)&&p4.lx==-32768&&p4.ly==-32768);
        d[0]=0x11;CHECK(!parse_ds4_usb_input(d,64,in4));   // Bluetooth report: not supported
        OutState s4;s4.rumL=200;s4.rumR=90;s4.light={10,20,30};uint8_t o4[DS4_OUTPUT_SIZE];build_ds4_output(s4,o4);
        CHECK(o4[0]==0x05&&o4[1]==0x03&&o4[4]==90&&o4[5]==200&&o4[6]==10&&o4[7]==20&&o4[8]==30&&o4[31]==0);}
    // --- trigger effect encodings (Sony official modes) ---
    CHECK(bytes(fx_off(),{0x05}));
    CHECK(bytes(fx_feedback(0,8),{0x21,0xFF,0x03,0xFF,0xFF,0xFF,0x3F}));          // all zones, force 7 in each 3-bit slot
    CHECK(bytes(fx_feedback(9,1),{0x21,0x00,0x02,0x00,0x00,0x00,0x00}));          // zone 9 only, force 0
    CHECK(bytes(fx_feedback(5,0),{0x05}));
    CHECK(bytes(fx_weapon(2,7,8),{0x25,0x84,0x00,0x07}));
    CHECK(bytes(fx_weapon(3,5,6),{0x25,0x28,0x00,0x05}));
    CHECK(bytes(fx_vibration(0,8,30),{0x26,0xFF,0x03,0xFF,0xFF,0xFF,0x3F,0,0,30}));
    CHECK(bytes(fx_bow(1,6,4,6),{0x22,0x42,0x00,0x2B,0x00}));                     // (4-1) | (6-1)<<3
    CHECK(bytes(fx_galloping(1,9,2,4,3),{0x23,0x02,0x02,0x14,0x03}));             // foot2 | foot1<<3
    CHECK(bytes(fx_machine(1,9,1,5,7,2),{0x27,0x02,0x02,0x29,0x07,0x02}));
    {int s[10]={0,2,2,2,2,0,0,0,0,8};TriggerFx f=fx_multi_feedback(s);
        CHECK(f.b[0]==0x21&&f.b[1]==0x1E&&f.b[2]==0x02);
        uint32_t force=f.b[3]|(f.b[4]<<8)|(f.b[5]<<16)|(static_cast<uint32_t>(f.b[6])<<24);
        CHECK(((force>>3)&7)==1&&((force>>12)&7)==1&&((force>>27)&7)==7&&((force>>15)&7)==0);}
    {TriggerFx f=fx_slope(1,9,1,8);uint32_t force=f.b[3]|(f.b[4]<<8)|(f.b[5]<<16)|(static_cast<uint32_t>(f.b[6])<<24);
        CHECK(((force>>3)&7)==0&&((force>>27)&7)==7);int prev=-1,ok=1;for(int i=1;i<10;++i){int v=(force>>(3*i))&7;if(v<prev)ok=0;prev=v;}CHECK(ok);}
    // Strength scaling: 100 % keeps the value, 25 % of 8 is 3, never 0 for a real effect.
    CHECK(scale(8,100)==8&&scale(5,100)==5&&scale(8,70)==6&&scale(8,25)==3&&scale(1,25)==1&&scale(8,0)==0&&scale(0,100)==0);
    CHECK(zone_raw(0)==0&&zone_raw(10)==255&&zone_raw(5)==128);

    // --- weapons: every eWeaponType has a profile, guns fire at the click, melee stays loose ---
    CHECK(std::string_view(weapon_profile(22).name)=="pistol"&&std::string_view(weapon_profile(24).name)=="deagle");
    CHECK(std::string_view(weapon_profile(25).name)=="shotgun"&&std::string_view(weapon_profile(31).name)=="m4");
    CHECK(std::string_view(weapon_profile(34).name)=="sniper"&&std::string_view(weapon_profile(38).name)=="minigun");
    CHECK(std::string_view(weapon_profile(43).name)=="camera"&&std::string_view(weapon_profile(46).name)=="none");
    CHECK(std::string_view(weapon_profile(9).name)=="chainsaw"&&std::string_view(weapon_profile(16).name)=="grenade");
    CHECK(std::string_view(weapon_profile(200).name)=="none");
    for(unsigned t=0;t<47;++t){const WeaponProfile& w=weapon_profile(t);
        if(w.kind==K_CLICK||w.kind==K_HEAVY)CHECK(w.a>=2&&w.a<w.b&&w.b<=8);
        WeaponState s;s.type=t;TriggerPair tp=weapon_triggers(s,70,true);
        CHECK(tp.firePoint>=0&&tp.firePoint<250);
        if(w.kind==K_NONE)CHECK(tp.r2==fx_off()&&tp.l2==fx_off()&&tp.firePoint==0);
        if(w.kind==K_BOW||w.kind==K_SPRAY||w.kind==K_CHAINSAW)CHECK(tp.firePoint==0);}
    {WeaponState s;s.type=22;TriggerPair t=weapon_triggers(s,70,true);CHECK(t.r2.b[0]==0x25&&t.l2.b[0]==0x21&&t.firePoint>100&&t.firePoint<160);
        TriggerPair t2=weapon_triggers(s,70,false);CHECK(t2.firePoint==0);
        s.hasAmmo=false;t=weapon_triggers(s,70,true);CHECK(t.r2==fx_off()&&t.firePoint==0);
        s.hasAmmo=true;s.reloading=true;t=weapon_triggers(s,70,true);CHECK(t.r2==fx_off());}
    {WeaponState s;s.type=25;s.sinceShotMs=50;CHECK(weapon_triggers(s,70,true).r2.b[0]==0x26);s.sinceShotMs=500;CHECK(weapon_triggers(s,70,true).r2.b[0]==0x25);}
    {WeaponState s;s.type=30;CHECK(weapon_triggers(s,70,true).r2.b[0]==0x21);s.firing=true;TriggerPair t=weapon_triggers(s,70,true);CHECK(t.r2.b[0]==0x26&&t.r2.b[9]==10);}
    {WeaponState s;s.type=28;s.firing=true;CHECK(weapon_triggers(s,70,true).r2.b[9]==22);}   // Uzi buzzes faster than the AK
    {WeaponState s;s.type=34;TriggerPair t=weapon_triggers(s,70,true);CHECK(t.r2.b[0]==0x25&&t.r2.b[3]==7&&t.firePoint>zone_raw(6));   // bolt: crisp break at full force
        s.sinceShotMs=60;CHECK(weapon_triggers(s,70,true).r2.b[0]==0x26);s.sinceShotMs=400;s.cycling=true;CHECK(weapon_triggers(s,70,true).r2==fx_off());
        s.type=33;s.cycling=false;CHECK(weapon_triggers(s,70,true).r2.b[0]==0x25);}
    {WeaponState s;s.type=0;CHECK(weapon_triggers(s,100,true).r2==fx_off());}
    {WeaponState s;s.type=22;CHECK(weapon_triggers(s,0,true).r2==fx_off());}

    // --- vehicles ---
    CHECK(vehicle_kind(0)==V_CAR&&vehicle_kind(9)==V_BIKE&&vehicle_kind(10)==V_BMX&&vehicle_kind(5)==V_BOAT&&vehicle_kind(3)==V_AIR&&vehicle_kind(6)==V_TRAIN&&vehicle_kind(11)==V_NONE);
    {VehicleState v;v.kind=V_CAR;v.kmh=0;TriggerFx slow=vehicle_triggers(v,100,true).r2;v.kmh=200;TriggerFx fast=vehicle_triggers(v,100,true).r2;
        uint32_t fs=slow.b[3]|(slow.b[4]<<8)|(slow.b[5]<<16)|(static_cast<uint32_t>(slow.b[6])<<24),ff=fast.b[3]|(fast.b[4]<<8)|(fast.b[5]<<16)|(static_cast<uint32_t>(fast.b[6])<<24);
        CHECK(((ff>>27)&7)>((fs>>27)&7));}   // resistance at the end of the travel grows with speed
    {VehicleState v;v.kind=V_CAR;v.kmh=80;v.braking=true;v.skidding=true;TriggerPair t=vehicle_triggers(v,25,true);CHECK(t.l2.b[0]==0x26&&std::string_view(t.name)=="abs");
        v.braking=false;v.skidding=false;v.handbrake=true;CHECK(std::string_view(vehicle_triggers(v,25,true).name)=="handbrake");
        v.handbrake=false;CHECK(vehicle_triggers(v,25,true).l2.b[0]==0x21);}
    {VehicleState v;v.kind=V_AIR;TriggerPair t=vehicle_triggers(v,25,true);CHECK(t.l2==t.r2&&t.r2.b[0]==0x21);}
    {VehicleState v;v.kind=V_BMX;v.kmh=20;CHECK(vehicle_triggers(v,25,true).r2.b[0]==0x23);}
    {VehicleState v;v.kind=V_TRAIN;CHECK(vehicle_triggers(v,25,true).r2==fx_off());}
    {VehicleState v;v.kind=V_CAR;v.health=200;CHECK(vehicle_triggers(v,25,true).r2.b[0]==0x27);}

    // --- rumble envelope ---
    Rumble rb;rb.add(200,100,100,1000);float l,rr;rb.level(1000,l,rr);CHECK(l==200&&rr==100);
    rb.level(1050,l,rr);CHECK(l>99&&l<101);rb.level(1100,l,rr);CHECK(l==0&&rr==0);
    for(int i=0;i<30;++i)rb.add(8.f*i,0,1000,2000);rb.level(2000,l,rr);CHECK(l==232);   // strongest pulses survive

    // --- lights ---
    Rgb h=health_color(1.f,0);CHECK(h.g>200&&h.r<20);h=health_color(0.5f,0);CHECK(h.r==255&&h.g>150);h=health_color(0.25f,0);CHECK(h.r==255&&h.g<120);
    Rgb low=health_color(0.1f,0),lowLater=health_color(0.1f,500);CHECK(low.r!=lowLater.r);   // heartbeat
    CHECK(gauge_leds(0)==0&&gauge_leds(0.01f)==0x01&&gauge_leds(0.4f)==0x03&&gauge_leds(1.f)==0x1F&&gauge_leds(2.f)==0x1F);
    CHECK(speed_color(0).g>200&&speed_color(1).r==255&&speed_color(1).g==0);
    Rgb pc;CHECK(!police_flash(0,0,pc));CHECK(police_flash(1,0,pc)&&pc.r==255);CHECK(police_flash(1,500,pc)&&pc.b==255);
    CHECK(dim(Rgb{200,100,50},0.5f).r==100);

    // --- output report 0x02 (USB, 48 bytes) ---
    OutState s;s.rumL=0xAA;s.rumR=0x55;s.r2=fx_weapon(2,7,8);s.l2=fx_feedback(0,8);s.light={1,2,3};s.leds=0x1F|0x20;s.micLed=true;
    uint8_t out[USB_OUTPUT_SIZE];build_output(s,false,true,false,out);
    CHECK(out[0]==0x02&&out[1]==0x0F&&out[2]==0x15&&out[3]==0x55&&out[4]==0xAA&&out[9]==1);
    CHECK(out[11]==0x25&&out[12]==0x84&&out[14]==0x07&&out[22]==0x21&&out[23]==0xFF);
    CHECK(out[39]==0x02&&out[42]==0x02&&out[44]==0x1F&&out[45]==1&&out[46]==2&&out[47]==3);
    build_output(s,true,false,true,out);CHECK(out[1]==0x0E&&out[2]==0x1D&&out[39]==0x04&&out[42]==0);
    // Recipes: steps differ by surface (grain clouds on loose ground), glass scatters, crash zones differ.
    {uint32_t rng=1;hx::Recipe sand=hx::footstep(hx::S_SAND,1,0,rng),metal=hx::footstep(hx::S_METAL,1,0,rng),conc=hx::footstep(hx::S_CONCRETE,1,0,rng);
        CHECK(sand.n>=10&&metal.n==3&&conc.n==2);float maxDelay=0;for(int i=0;i<sand.n;++i)maxDelay=std::max(maxDelay,sand.p[i].delay);CHECK(maxDelay>0.05f&&maxDelay<0.2f);
        for(int st=0;st<hx::S_COUNT;++st){hx::Recipe r=hx::footstep(static_cast<hx::Surface>(st),1,0.4f,rng);CHECK(r.n>=2);}
        hx::Recipe g=hx::glass_break(1,0,rng);CHECK(g.n==13);hx::Recipe side=hx::crash(1,hx::Z_SIDE,0.9f);CHECK(side.n==5&&side.p[0].pan==0.9f);
        CHECK(hx::crash(1,hx::Z_FRONT,0).n==2&&hx::crash(1,hx::Z_REAR,0).n==1);
        // A delayed hit stays silent until its time, then plays.
        const int RATE=48000;static float L2[RATE/2],R2[RATE/2];hx::Synth d(RATE);d.trigger(hx::weapon_hit(22),1,0,0.1f);d.render(L2,R2,RATE/2);
        float early=0,late=0;for(int i=0;i<RATE/20;++i)early=std::max(early,std::fabs(L2[i]));for(int i=RATE/10;i<RATE/10+960;++i)late=std::max(late,std::fabs(L2[i]));
        CHECK(early==0&&late>0.3f);}
    OutState a,b;CHECK(a==b);b.r2=fx_feedback(1,2);CHECK(a!=b);
    // HD haptics: both rumble bits clear, motors silent, improved-rumble bit off; triggers still active.
    {OutState h;h.hd=true;h.rumL=200;h.rumR=100;uint8_t o2[USB_OUTPUT_SIZE];build_output(h,true,true,false,o2);
        CHECK(o2[1]==0x0C&&o2[3]==0&&o2[4]==0&&o2[39]==0x02);OutState c=h;c.hd=false;CHECK(c!=h);}

    // --- HD haptics synthesizer ---
    {const int RATE=48000;static float L[RATE],R[RATE];
        auto peak=[&](const float* b,int from,int to){float m=0;for(int i=from;i<to;++i)m=std::max(m,std::fabs(b[i]));return m;};
        hx::Synth s(RATE);s.render(L,R,4800);CHECK(peak(L,0,4800)==0&&s.active()==0);   // silence by default
        s.trigger(hx::weapon_hit(22),1.f,0.f);CHECK(s.active()==1);s.render(L,R,RATE/2);
        CHECK(peak(L,0,960)>0.3f&&peak(L,0,960)<=1.f);                  // strong in the first 20 ms, never above full scale
        CHECK(peak(L,RATE/4,RATE/2)<0.01f&&s.active()==0);               // a pistol crack is gone well before 250 ms
        s.trigger(hx::weapon_hit(25),1.f,-1.f);s.render(L,R,4800);CHECK(peak(L,0,4800)>0.3f&&peak(R,0,4800)<1e-6f);   // left hand only
        hx::Synth s2(RATE);s2.trigger(hx::weapon_hit(34),1.f,0.f);s2.render(L,R,RATE/2);float sniperTail=peak(L,RATE/10,RATE/5);
        hx::Synth s3(RATE);s3.trigger(hx::weapon_hit(28),1.f,0.f);s3.render(L,R,RATE/2);CHECK(sniperTail>peak(L,RATE/10,RATE/5));   // sniper rings longer than an uzi
        hx::Synth e(RATE);hx::Layers ly;hx::engine_layer(1,0.5f,1.f,false,ly);e.set(ly);e.render(L,R,RATE/4);CHECK(peak(L,RATE/8,RATE/4)>0.1f);
        e.set(hx::Layers{});e.render(L,R,RATE/2);CHECK(peak(L,RATE/4,RATE/2)<0.01f);   // layers fade out when the game stops asking
        hx::Synth t(RATE);for(int i=0;i<40;++i)t.trigger(hx::explosion_hit(1),1.5f,0);t.render(L,R,RATE/4);CHECK(peak(L,0,RATE/4)<=1.f);   // limiter
        for(int st=0;st<hx::S_COUNT;++st){hx::Hit f=hx::footstep_hit(static_cast<hx::Surface>(st));CHECK(f.gain>0&&f.dur>0&&f.decay>0);}
        for(unsigned w:{22u,23u,24u,25u,26u,27u,28u,29u,30u,31u,32u,33u,34u,35u,36u,38u}){hx::Hit h=hx::weapon_hit(w);CHECK(h.gain>0&&h.f0>=h.f1);}
        CHECK(hx::weapon_hit(0).gain==0&&hx::weapon_hit(16).gain==0);
        CHECK(hx::step_rate(1.4f)<hx::step_rate(6.f)&&hx::step_rate(0)>=1.6f&&hx::step_rate(20)<=3.6f);
        hx::Layers bike,car,truck;hx::engine_layer(2,0.5f,0,false,bike);hx::engine_layer(1,0.5f,0,false,car);hx::engine_layer(1,0.5f,0,true,truck);
        CHECK(bike.engineHz>car.engineHz&&car.engineHz>truck.engineHz&&truck.engineAmp>car.engineAmp);}

    if(failures){std::printf("%d check(s) failed\n",failures);return 1;}
    std::printf("all DualSense core checks passed\n");return 0;
}
