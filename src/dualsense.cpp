// Alek's DualSense SA 2.0 - native DualSense (USB) for GTA San Andreas 1.0 US.
// The pad is read through HID and shown to the whole process as XInput controller 0, so the game, GInput, the Photo
// Mode, PadDetach and the other XInput readers keep working unchanged. Adaptive triggers, rumble and lights come from
// game state sampled on the game thread. Pure logic lives in ds_core.h (tested by test_ds.cpp).
// Addresses: GTA SA 1.0 US, checked against DK22Pac plugin-sdk VALIDATE_OFFSET lines and the 1.0 US executable.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <d3d9.h>
#include <shlobj.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ds_core.h"
#include "ds_haptics.h"

namespace {
// ---------- game addresses (plugin-sdk) ----------
constexpr uintptr_t PLAYER=0xB7CD98;                    // CWorld::Players[0] (CPlayerInfo, m_pPed at +0)
constexpr uintptr_t PLAYER_WANTED=PLAYER+0x4;           // CPlayerInfo::m_PlayerData.m_pWanted
constexpr uintptr_t PLAYER_MAX_HEALTH=PLAYER+0x14F, PLAYER_MAX_ARMOUR=PLAYER+0x150;   // uint8
constexpr uintptr_t MENU_ACTIVE=0xBA6748+0x5C;          // FrontEndMenuManager.m_bMenuActive
constexpr uintptr_t CODE_PAUSE=0xB7CB48, USER_PAUSE=0xB7CB49, GAME_TIME_MS=0xB7CB84;
constexpr uintptr_t TIME_STEP=0xB7CB5C,TIME_SCALE=0xB7CB64;   // CTimer::ms_fTimeStep, ms_fTimeScale
constexpr uintptr_t PROJECTILE_INFO=0xC891A8,PROJECTILES=0xC89110;constexpr int PROJECTILE_COUNT=32,PROJECTILE_INFO_SIZE=0x24;   // gaProjectileInfo (type +0, active +0x10, last pos +0x14), ms_apProjectile
constexpr uintptr_t CUTSCENE_RUNNING=0xB5F851;          // CCutsceneMgr::ms_running
constexpr uintptr_t PAD0_DISABLE_CONTROLS=0xB73458+0x10E; // CPad::Pads[0].DisablePlayerControls (Photo Mode sets 0x8000)
constexpr uintptr_t EXPLOSIONS=0xC88950;constexpr int EXPLOSION_COUNT=16,EXPLOSION_SIZE=0x7C;   // CExplosion: pos +4, created +0x2C
constexpr uintptr_t SURFACE_INFOS=0xB79538;constexpr unsigned SURFACE_COUNT=179;   // roughness = (dword[+0x94+id*12]>>23)&3 (0x55E810)
// CPed
constexpr uintptr_t PED_MATRIX=0x14,PED_POS=0x4,PED_FLAGS2=0x46D,PED_HEALTH=0x540,PED_ARMOUR=0x548,PED_VEHICLE=0x58C,PED_WEAPONS=0x5A0,PED_WEAPON_SLOT=0x718;
constexpr uintptr_t WEAPON_SIZE=0x1C;                   // CWeapon: type +0, state +4, clip +8, total +0xC, nextShot +0x10
// CVehicle / CAutomobile / CBike
constexpr uintptr_t VEH_MOVE_SPEED=0x44,VEH_FLAGS=0x428,VEH_DRIVER=0x460,VEH_GAS=0x49C,VEH_BRAKE=0x4A0,VEH_HEALTH=0x4C0,VEH_SUBCLASS=0x594;
constexpr uintptr_t CAR_COLPOINTS=0x724,CAR_SUSP=0x7D4,CAR_SUSP_PREV=0x7E4,CAR_ON_GROUND=0x961,CAR_WHEEL_STATE=0x968;
constexpr uintptr_t BIKE_COLPOINTS=0x660,BIKE_RATIOS=0x710,BIKE_ON_GROUND=0x805,BIKE_WHEEL_STATE=0x80C;
constexpr uintptr_t COLPOINT_SIZE=0x2C,COLPOINT_SURFACE_B=0x24;
constexpr uintptr_t CAR_AUDIO_REVS=0x964,BIKE_AUDIO_REVS=0x808,VEH_MASS=0x8C;   // m_fGasPedalAudioRevs (CAutomobile, CBike), CPhysical::m_fMass
constexpr uintptr_t VEH_COLLISION_POS=0xEC;   // CPhysical::m_vecLastCollisionPosn
// Animation (plugin-sdk): RpAnimBlendClumpGetMainAssociation 0x4D6910 (cdecl clump, &second, &blend); CEntity::m_pRwClump +0x18;
// CAnimBlendAssociation: m_pHierarchy +0x14, m_fBlendAmount +0x18, m_fCurrentTime +0x20, m_nAnimId +0x2C; CAnimBlendHierarchy::m_fTotalTime +0x10.
constexpr uintptr_t FN_GET_MAIN_ASSOCIATION=0x4D6910,PED_CLUMP=0x18,ASSOC_HIERARCHY=0x14,ASSOC_BLEND=0x18,ASSOC_CURRENT_TIME=0x20,ASSOC_ANIM_ID=0x2C,HIER_TOTAL_TIME=0x10;
using GetMainAssocFn=uintptr_t (__cdecl*)(uintptr_t,uintptr_t*,float*);
constexpr uintptr_t PED_FLAGS1=0x46C,PED_MOVE_SPEED=0x44,PED_CONTACT_SURFACE=0xBA;   // bIsStanding = bit 0, CPhysical::m_vecMoveSpeed, m_nContactSurface
// CPad::GetPad (0x53FB70) bytes in 1.0 US: proves the executable before any game memory is read.
constexpr uintptr_t VERSION_PROBE=0x53FB70;
const BYTE versionBytes[15]={0x8B,0x44,0x24,0x04,0x69,0xC0,0x34,0x01,0x00,0x00,0x05,0x58,0x34,0xB7,0x00};

struct Vec3{float x,y,z;};
template<class T> T mem(uintptr_t a){T v;std::memcpy(&v,reinterpret_cast<const void*>(a),sizeof v);return v;}
bool plausible(uintptr_t p){return p>=0x10000&&p<0x7FFF0000;}
bool readable(uintptr_t a,size_t n){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<const void*>(a),&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    return a+n<=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;}

// ---------- settings ----------
struct Settings{
    bool enabled=true,allowBackground=false,touchpadAsBack=true,muteToggles=true,logEvents=true;int deadzone=0;int controller=0;   // 0 auto (DualSense first), 1 DualSense only, 2 DualShock 4 only
    bool triggers=true,weaponTriggers=true,fireAtClick=true,drivingTriggers=true,speedResponse=true;std::atomic<int> weaponStrength{70},drivingStrength{25};   // atomic: changed in game with Mute + D-pad
    std::atomic<int> vibration{65};int drivingVibration=60;bool gameRumble=true,shots=true,damage=true,explosions=true,crashes=true,terrain=true;
    bool xboxRumble=true;   // 2.1: with no Sony pad, the effects go as rumble to a real XInput pad (Xbox, DualShock 3 via DsHidMini)
    bool hdHaptics=true,footsteps=true,soundShots=true;   // soundShots (2.2): gunshots felt from the EarShot weapon sounds when found   // v2: HD haptics through the pad's audio channels 3-4 (falls back to rumble if unavailable)
    bool lightbar=true,healthColor=true,armourLeds=true,speedometer=true,policeFlashes=true;int brightness=100,speedKmh=160;ds::Rgb menu{0,80,200};
} cfg;

HMODULE self=nullptr;wchar_t folder[MAX_PATH]{},iniPath[MAX_PATH]{},logPath[MAX_PATH]{};
CRITICAL_SECTION logLock,stateLock;unsigned long long startTick=0;DWORD mainThread=0;bool gameOk=false,dryRun=false;   // dryRun: not inside GTA SA (offline harness), the pad is read but never written
std::atomic<bool> padConnected{false},stopping{false},userTriggersOff{false},playerDriving{false};
std::atomic<int> padModel{0};   // 0 DualSense, 1 DualSense Edge, 2 DualShock 4 (input, rumble and lightbar only)
std::atomic<bool> padBluetooth{false};   // over Bluetooth Windows exposes no haptics audio channel: classic rumble only
bool isDs4(){return padModel==2;}
const char* padName(){return padModel==2?"DualShock 4":padModel==1?"DualSense Edge":"DualSense";}

void log(const char* message){
    EnterCriticalSection(&logLock);
    FILE* f=nullptr;_wfopen_s(&f,logPath,L"a");
    if(f){fprintf(f,"[%9.3f] %s\n",static_cast<double>(GetTickCount64()-startTick)/1000.0,message);fclose(f);}
    LeaveCriticalSection(&logLock);
}
void logf(const char* format,...){char text[768];va_list args;va_start(args,format);vsnprintf(text,sizeof text,format,args);va_end(args);log(text);}
// Event lines: the first 25 of each kind, then every 100th, so a long session keeps a readable log.
enum Ev{EV_SHOT,EV_MELEE,EV_DAMAGE,EV_EXPLOSION,EV_CRASH,EV_LANDING,EV_TRIGGER,EV_STEP,EV_COUNT};
const char* evName[EV_COUNT]={"shots","melee hits","damage","explosions","crashes","landings","trigger changes","steps"};
unsigned evCount[EV_COUNT]{};
void event(Ev e,const char* format,...){
    unsigned n=++evCount[e];if(!cfg.logEvents||(n>25&&n%100))return;
    char text[512];va_list args;va_start(args,format);vsnprintf(text,sizeof text,format,args);va_end(args);logf("event #%u %s",n,text);
}
const char* where(uintptr_t address,char* out,size_t size){
    HMODULE module=nullptr;char path[MAX_PATH]{};
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(address),&module)&&module){
        GetModuleFileNameA(module,path,MAX_PATH);const char* name=strrchr(path,'\\');
        std::snprintf(out,size,"%s+%lX",name?name+1:path,static_cast<unsigned long>(address-reinterpret_cast<uintptr_t>(module)));
    }else std::snprintf(out,size,"%08lX",static_cast<unsigned long>(address));
    return out;
}
bool gameFocused(){DWORD pid=0;HWND w=GetForegroundWindow();if(w)GetWindowThreadProcessId(w,&pid);return pid==GetCurrentProcessId();}

// Settings: code defaults -> the mod INI (MO2) -> the player's file in Documents (menu and Mute + D-pad changes).
wchar_t userIni[MAX_PATH]{};int menuOpenWith=0,menuKey=VK_F11,menuLanguage=0;const wchar_t* readPath=nullptr;   // menuLanguage: 0 auto (game language), 1 Spanish, 2 English
int iniInt(const wchar_t* section,const wchar_t* key,int def){return static_cast<int>(GetPrivateProfileIntW(section,key,def,readPath));}
bool iniBool(const wchar_t* section,const wchar_t* key,bool def){return iniInt(section,key,def?1:0)!=0;}
void readSettings(){   // every key falls back to the value already loaded
    Settings& c=cfg;
    c.enabled=iniBool(L"General",L"Enabled",c.enabled);c.allowBackground=iniBool(L"General",L"AllowBackgroundInput",c.allowBackground);
    c.touchpadAsBack=iniBool(L"General",L"TouchpadAsBack",c.touchpadAsBack);c.muteToggles=iniBool(L"General",L"MuteTogglesTriggers",c.muteToggles);
    c.deadzone=std::clamp(iniInt(L"General",L"StickDeadzone",c.deadzone),0,50);c.controller=std::clamp(iniInt(L"General",L"Controller",c.controller),0,2);c.logEvents=iniBool(L"General",L"LogEvents",c.logEvents);
    c.triggers=iniBool(L"Triggers",L"AdaptiveTriggers",c.triggers);c.weaponTriggers=iniBool(L"Triggers",L"WeaponTriggers",c.weaponTriggers);
    c.weaponStrength=std::clamp(iniInt(L"Triggers",L"WeaponStrength",c.weaponStrength),0,150);c.fireAtClick=iniBool(L"Triggers",L"FireAtClick",c.fireAtClick);
    c.drivingTriggers=iniBool(L"Triggers",L"DrivingTriggers",c.drivingTriggers);c.drivingStrength=std::clamp(iniInt(L"Triggers",L"DrivingStrength",c.drivingStrength),0,150);
    c.speedResponse=iniBool(L"Triggers",L"SpeedResponse",c.speedResponse);
    c.vibration=std::clamp(iniInt(L"Vibration",L"Vibration",c.vibration),0,100);c.drivingVibration=std::clamp(iniInt(L"Vibration",L"DrivingVibration",c.drivingVibration),0,100);
    c.gameRumble=iniBool(L"Vibration",L"GameRumble",c.gameRumble);
    c.shots=iniBool(L"Vibration",L"Shots",c.shots);c.damage=iniBool(L"Vibration",L"Damage",c.damage);c.explosions=iniBool(L"Vibration",L"Explosions",c.explosions);
    c.crashes=iniBool(L"Vibration",L"Crashes",c.crashes);c.terrain=iniBool(L"Vibration",L"Terrain",c.terrain);
    c.xboxRumble=iniBool(L"Vibration",L"XboxRumble",c.xboxRumble);
    c.hdHaptics=iniBool(L"Haptics",L"HD",c.hdHaptics);c.footsteps=iniBool(L"Haptics",L"Footsteps",c.footsteps);c.soundShots=iniBool(L"Haptics",L"SoundShots",c.soundShots);
    c.lightbar=iniBool(L"Lighting",L"Lightbar",c.lightbar);c.brightness=std::clamp(iniInt(L"Lighting",L"Brightness",c.brightness),0,100);
    c.healthColor=iniBool(L"Lighting",L"HealthColor",c.healthColor);c.armourLeds=iniBool(L"Lighting",L"ArmourLeds",c.armourLeds);
    c.speedometer=iniBool(L"Lighting",L"Speedometer",c.speedometer);c.speedKmh=std::clamp(iniInt(L"Lighting",L"SpeedometerKmh",c.speedKmh),40,400);
    c.policeFlashes=iniBool(L"Lighting",L"PoliceFlashes",c.policeFlashes);
    c.menu={static_cast<uint8_t>(std::clamp(iniInt(L"Lighting",L"MenuRed",c.menu.r),0,255)),static_cast<uint8_t>(std::clamp(iniInt(L"Lighting",L"MenuGreen",c.menu.g),0,255)),
        static_cast<uint8_t>(std::clamp(iniInt(L"Lighting",L"MenuBlue",c.menu.b),0,255))};
    menuOpenWith=std::clamp(iniInt(L"Menu",L"OpenWith",menuOpenWith),0,2);menuLanguage=std::clamp(iniInt(L"Menu",L"Language",menuLanguage),0,2);menuKey=std::clamp(iniInt(L"Menu",L"Key",menuKey),0,255);
}
void loadSettings(){
    Settings& c=cfg;   // code defaults first (also used by "Restablecer todo")
    c.enabled=c.touchpadAsBack=c.muteToggles=c.logEvents=true;c.allowBackground=false;c.deadzone=0;c.controller=0;
    c.triggers=c.weaponTriggers=c.fireAtClick=c.drivingTriggers=c.speedResponse=true;c.weaponStrength=70;c.drivingStrength=25;
    c.vibration=65;c.drivingVibration=60;c.gameRumble=c.shots=c.damage=c.explosions=c.crashes=c.terrain=c.xboxRumble=true;c.hdHaptics=c.footsteps=c.soundShots=true;
    c.lightbar=c.healthColor=c.armourLeds=c.speedometer=c.policeFlashes=true;c.brightness=100;c.speedKmh=160;c.menu={0,80,200};menuOpenWith=0;menuKey=VK_F11;menuLanguage=0;
    readPath=iniPath;readSettings();
    bool user=*userIni&&GetFileAttributesW(userIni)!=INVALID_FILE_ATTRIBUTES;if(user){readPath=userIni;readSettings();}
    logf("settings%s: triggers=%d weapons=%d%% fireAtClick=%d driving=%d%% speedResponse=%d vibration=%d%% (driving %d%%) gameRumble=%d xbox=%d lightbar=%d brightness=%d%% police=%d speedometer=%dkm/h touchpadBack=%d muteToggles=%d menu=%d key=%d",
        user?" (with the player's file)":"",c.triggers,c.weaponStrength.load(),c.fireAtClick,c.drivingStrength.load(),c.speedResponse,c.vibration.load(),c.drivingVibration,c.gameRumble,c.xboxRumble,c.lightbar,c.brightness,
        c.policeFlashes,c.speedKmh,c.touchpadAsBack,c.muteToggles,menuOpenWith,menuKey);
}

// ---------- shared state (device thread <-> game thread) ----------
ds::Input lastInput;ds::XPad pad;DWORD packet=0;
ds::OutState desired;unsigned long long desiredAt=0;int firePoint=0;
std::atomic<unsigned> gameRumbleL{0},gameRumbleR{0};

ds::OutState neutralOut(){ds::OutState o;o.light=ds::dim(cfg.menu,cfg.brightness/100.f);o.leds=0x04;o.micLed=userTriggersOff;return o;}

#include "ds_audio.inc"
#include "ds_menu.inc"

// ---------- XInput bridge ----------
// Every loaded xinput*.dll gets its exports patched at the entry, so all readers in the process (GInput imports
// xinput1_3, the Photo Mode loads xinput1_4, UltimateFP tries several) see the same controller 0.
enum Fn{F_GET,F_SET,F_CAPS,F_BATTERY,F_GETEX,F_COUNT};
const char* fnName[F_COUNT]={"XInputGetState","XInputSetState","XInputGetCapabilities","XInputGetBatteryInformation","XInputGetStateEx(#100)"};
constexpr int DLLS=5;const wchar_t* dllName[DLLS]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll",L"xinput1_2.dll",L"xinput1_1.dll"};
struct HookedDll{HMODULE module=nullptr;void* original[F_COUNT]{};bool done=false;};
HookedDll dlls[DLLS];
using GetStateFn=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
using SetStateFn=DWORD (WINAPI*)(DWORD,XINPUT_VIBRATION*);
using CapsFn=DWORD (WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*);
using BatteryFn=DWORD (WINAPI*)(DWORD,BYTE,XINPUT_BATTERY_INFORMATION*);

void gameTick();
std::atomic<bool> xboxActive{false};   // a real XInput pad in slot 0 gets the mod's rumble (no Sony pad connected)
// First call from each call site is logged with its module, so the log shows who reads the pad.
CRITICAL_SECTION callerLock;uintptr_t callerSeen[48]{};int callerCount=0;
void noteCaller(int d,int f,void* ret){
    uintptr_t r=reinterpret_cast<uintptr_t>(ret);
    EnterCriticalSection(&callerLock);bool seen=false;
    for(int i=0;i<callerCount;++i)if(callerSeen[i]==r){seen=true;break;}
    if(!seen&&callerCount<48)callerSeen[callerCount++]=r;
    LeaveCriticalSection(&callerLock);
    if(seen||callerCount>=48)return;
    char w[160];char dn[32];WideCharToMultiByte(CP_UTF8,0,dllName[d],-1,dn,sizeof dn,nullptr,nullptr);
    logf("first %s from %s via %s (%s thread %lu)",fnName[f],where(r,w,sizeof w),dn,GetCurrentThreadId()==mainThread?"game":"other",static_cast<unsigned long>(GetCurrentThreadId()));
}
DWORD onGetState(int d,int f,DWORD user,XINPUT_STATE* s,void* ret){
    noteCaller(d,f,ret);
    if(user==0&&GetCurrentThreadId()==mainThread)gameTick();
    if(user==0&&padConnected){
        if(!s)return ERROR_BAD_ARGUMENTS;
        ds::XPad p;DWORD n;int fp;
        unsigned long long at;EnterCriticalSection(&stateLock);p=pad;n=packet;fp=firePoint;at=desiredAt;LeaveCriticalSection(&stateLock);
        if(GetTickCount64()>at+350)fp=0;   // stale game sample: never hold the trigger back
        if((!cfg.allowBackground&&!gameFocused())||menuOpen){p=ds::XPad{};fp=0;}   // the menu owns the pad while open
        if(padHold){if(!p.buttons&&p.lt<30&&p.rt<30)padHold=false;else{p=ds::XPad{};fp=0;}}
        p.rt=ds::remap_fire(p.rt,fp);
        if(f!=F_GETEX)p.buttons&=static_cast<uint16_t>(~ds::X_GUIDE);
        s->dwPacketNumber=n;s->Gamepad.wButtons=p.buttons;s->Gamepad.bLeftTrigger=p.lt;s->Gamepad.bRightTrigger=p.rt;
        s->Gamepad.sThumbLX=p.lx;s->Gamepad.sThumbLY=p.ly;s->Gamepad.sThumbRX=p.rx;s->Gamepad.sThumbRY=p.ry;
        return ERROR_SUCCESS;
    }
    auto fn=reinterpret_cast<GetStateFn>(dlls[d].original[f]);return fn?fn(user,s):ERROR_DEVICE_NOT_CONNECTED;
}
DWORD onSetState(int d,DWORD user,XINPUT_VIBRATION* v,void* ret){
    noteCaller(d,F_SET,ret);
    if(user==0&&padConnected){
        if(!v)return ERROR_BAD_ARGUMENTS;
        static bool first=true;if(first&&(v->wLeftMotorSpeed||v->wRightMotorSpeed)){first=false;log("first game rumble request received");}
        gameRumbleL=v->wLeftMotorSpeed>>8;gameRumbleR=v->wRightMotorSpeed>>8;return ERROR_SUCCESS;
    }
    if(user==0&&xboxActive){   // Xbox mode: the game's rumble is mixed with the mod's effects in xboxTick
        if(!v)return ERROR_BAD_ARGUMENTS;
        gameRumbleL=v->wLeftMotorSpeed>>8;gameRumbleR=v->wRightMotorSpeed>>8;return ERROR_SUCCESS;
    }
    auto fn=reinterpret_cast<SetStateFn>(dlls[d].original[F_SET]);return fn?fn(user,v):ERROR_DEVICE_NOT_CONNECTED;
}
DWORD onCaps(int d,DWORD user,DWORD flags,XINPUT_CAPABILITIES* c,void* ret){
    noteCaller(d,F_CAPS,ret);
    if(user==0&&padConnected){
        if(!c)return ERROR_BAD_ARGUMENTS;
        std::memset(c,0,sizeof *c);c->Type=XINPUT_DEVTYPE_GAMEPAD;c->SubType=XINPUT_DEVSUBTYPE_GAMEPAD;
        c->Gamepad.wButtons=0xF3FF;c->Gamepad.bLeftTrigger=c->Gamepad.bRightTrigger=0xFF;
        c->Gamepad.sThumbLX=c->Gamepad.sThumbLY=c->Gamepad.sThumbRX=c->Gamepad.sThumbRY=static_cast<SHORT>(0xFFC0);
        c->Vibration.wLeftMotorSpeed=c->Vibration.wRightMotorSpeed=0xFF;return ERROR_SUCCESS;
    }
    auto fn=reinterpret_cast<CapsFn>(dlls[d].original[F_CAPS]);return fn?fn(user,flags,c):ERROR_DEVICE_NOT_CONNECTED;
}
DWORD onBattery(int d,DWORD user,BYTE type,XINPUT_BATTERY_INFORMATION* b,void* ret){
    noteCaller(d,F_BATTERY,ret);
    if(user==0&&padConnected){if(!b)return ERROR_BAD_ARGUMENTS;b->BatteryType=BATTERY_TYPE_WIRED;b->BatteryLevel=BATTERY_LEVEL_FULL;return ERROR_SUCCESS;}
    auto fn=reinterpret_cast<BatteryFn>(dlls[d].original[F_BATTERY]);return fn?fn(user,type,b):ERROR_DEVICE_NOT_CONNECTED;
}
template<int D> DWORD WINAPI hookGet(DWORD u,XINPUT_STATE* s){return onGetState(D,F_GET,u,s,__builtin_return_address(0));}
template<int D> DWORD WINAPI hookGetEx(DWORD u,XINPUT_STATE* s){return onGetState(D,F_GETEX,u,s,__builtin_return_address(0));}
template<int D> DWORD WINAPI hookSet(DWORD u,XINPUT_VIBRATION* v){return onSetState(D,u,v,__builtin_return_address(0));}
template<int D> DWORD WINAPI hookCaps(DWORD u,DWORD f,XINPUT_CAPABILITIES* c){return onCaps(D,u,f,c,__builtin_return_address(0));}
template<int D> DWORD WINAPI hookBattery(DWORD u,BYTE t,XINPUT_BATTERY_INFORMATION* b){return onBattery(D,u,t,b,__builtin_return_address(0));}
template<int D> void* detourFor(int f){
    switch(f){case F_GET:return reinterpret_cast<void*>(&hookGet<D>);case F_SET:return reinterpret_cast<void*>(&hookSet<D>);
        case F_CAPS:return reinterpret_cast<void*>(&hookCaps<D>);case F_BATTERY:return reinterpret_cast<void*>(&hookBattery<D>);
        default:return reinterpret_cast<void*>(&hookGetEx<D>);}
}
void* detour(int d,int f){
    switch(d){case 0:return detourFor<0>(f);case 1:return detourFor<1>(f);case 2:return detourFor<2>(f);case 3:return detourFor<3>(f);default:return detourFor<4>(f);}
}

// Each DLL gets its own trampoline page: written while read-write, then read-execute for good (never RWX and never
// re-protected while another thread may be running it).
CRITICAL_SECTION hookLock;
// Builds the trampoline for one export into t (16 bytes). Only two known prologues, or another mod's E9 jump, are accepted.
bool buildTrampoline(const BYTE* entry,BYTE* t,char* why,size_t whySize){
    static const BYTE prologueA[5]={0x55,0x8B,0xEC,0x6A,0xFF};   // push ebp; mov ebp,esp; push -1   (xinput1_1..1_3)
    static const BYTE prologueB[5]={0x8B,0xFF,0x55,0x8B,0xEC};   // mov edi,edi; push ebp; mov ebp,esp (xinput1_4, 9_1_0)
    if(std::memcmp(entry,prologueA,5)==0||std::memcmp(entry,prologueB,5)==0){
        std::memcpy(t,entry,5);t[5]=0xE9;int32_t rel=static_cast<int32_t>(reinterpret_cast<intptr_t>(entry+5)-reinterpret_cast<intptr_t>(t+10));std::memcpy(t+6,&rel,4);return true;
    }
    if(entry[0]==0xE9){   // another mod already hooked it: chain to its target
        int32_t old;std::memcpy(&old,entry+1,4);const BYTE* dest=entry+5+old;
        t[0]=0xE9;int32_t rel=static_cast<int32_t>(reinterpret_cast<intptr_t>(dest)-reinterpret_cast<intptr_t>(t+5));std::memcpy(t+1,&rel,4);
        char w[128];std::snprintf(why,whySize,"chained to existing hook %s",where(reinterpret_cast<uintptr_t>(dest),w,sizeof w));return true;
    }
    std::snprintf(why,whySize,"unknown prologue %02X %02X %02X %02X %02X, left alone",entry[0],entry[1],entry[2],entry[3],entry[4]);return false;
}
// Writes "jmp target" over the entry with one 8-byte compare-exchange, so no thread can run a half-written jump.
bool patchEntry(BYTE* entry,void* target){
    DWORD old;if(!VirtualProtect(entry,8,PAGE_EXECUTE_READWRITE,&old))return false;
    uint64_t before;std::memcpy(&before,entry,8);uint64_t after=before;BYTE* a=reinterpret_cast<BYTE*>(&after);
    a[0]=0xE9;int32_t rel=static_cast<int32_t>(reinterpret_cast<intptr_t>(target)-reinterpret_cast<intptr_t>(entry+5));std::memcpy(a+1,&rel,4);
    if((reinterpret_cast<uintptr_t>(entry)&7)==0)InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(entry),static_cast<LONG64>(after),static_cast<LONG64>(before));
    else std::memcpy(entry,a,5);
    VirtualProtect(entry,8,old,&old);FlushInstructionCache(GetCurrentProcess(),entry,8);return true;
}
void hookDll(int d){
    EnterCriticalSection(&hookLock);
    HookedDll& h=dlls[d];HMODULE m=h.done?nullptr:GetModuleHandleW(dllName[d]);
    if(m){
        h.module=m;h.done=true;char dn[32];WideCharToMultiByte(CP_UTF8,0,dllName[d],-1,dn,sizeof dn,nullptr,nullptr);
        BYTE* page=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        BYTE* entry[F_COUNT]{};bool ready[F_COUNT]{};char why[F_COUNT][192]{};
        for(int f=0;page&&f<F_COUNT;++f){
            FARPROC p=f==F_GETEX?GetProcAddress(m,MAKEINTRESOURCEA(100)):GetProcAddress(m,fnName[f]);
            if(!p)continue;entry[f]=reinterpret_cast<BYTE*>(p);
            ready[f]=buildTrampoline(entry[f],page+f*16,why[f],sizeof why[f]);
        }
        DWORD old;
        if(page&&VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old)){
            FlushInstructionCache(GetCurrentProcess(),page,4096);int ok=0;
            for(int f=0;f<F_COUNT;++f){
                if(!entry[f])continue;
                if(ready[f]){h.original[f]=page+f*16;   // original is valid before the jump goes live
                    if(patchEntry(entry[f],detour(d,f))){++ok;if(*why[f])logf("%s %s: %s",dn,fnName[f],why[f]);}
                    else{h.original[f]=nullptr;logf("%s %s NOT hooked: VirtualProtect failed",dn,fnName[f]);}}
                else logf("%s %s NOT hooked: %s",dn,fnName[f],why[f]);
            }
            logf("%s: %d functions bridged",dn,ok);
        }else logf("%s: no trampoline page, not bridged",dn);
    }
    LeaveCriticalSection(&hookLock);
}
void hookLoadedDlls(){for(int d=0;d<DLLS;++d)hookDll(d);}

// ---------- game telemetry (game thread only) ----------
struct Tracker{
    bool init=false;uintptr_t ped=0,veh=0;int slot=-1;unsigned type=0,total=0,state=0,nextShot=0;float hp=0,vhp=0;
    unsigned explosion[EXPLOSION_COUNT]{};unsigned long long lastShotAt=0,airSince=0;float bikePrev[4]{};
    const char* block="startup";const char* profile="";const char* base="";bool driver=false;int subClass=-1;
    float stepPhase=0.5f;bool stepLeft=false,cycling=false;unsigned long long footAirSince=0,lastNow=0;   // HD footsteps, bolt state
    uintptr_t animAssoc=0;float animPrev=0;   // walk animation that drives the steps
    bool projMolotov[32]{};Vec3 projPos[32]{};unsigned long long lastGlassAt=0;   // molotovs in flight (some grenade mods never add an explosion for them)
} tr;
// Camera look direction (CCamera::m_mCameraMatrix forward, as the Photo Mode reads it) for left/right panning.
constexpr uintptr_t CAMERA_FORWARD=0xB6F028+0x974+0x10;
// Footstep surface from SurfaceInfos flags2 (dword at +0x98+id*12; bits verified against IsAudioConcrete..IsAudioTile,
// 0x55EA30..0x55EB30): 10 concrete, 11 grass, 12 sand, 13 gravel, 14 wood, 15 water, 16 metal, 17 long grass, 18 tile.
hx::Surface footSurface(unsigned id){
    if(id>=SURFACE_COUNT)return hx::S_DEFAULT;uint32_t f=mem<uint32_t>(SURFACE_INFOS+0x98+id*12);
    if(f&(1u<<16))return hx::S_METAL;if(f&(1u<<14))return hx::S_WOOD;if(f&(1u<<15))return hx::S_WATER;if(f&(1u<<13))return hx::S_GRAVEL;
    if(f&(1u<<12))return hx::S_SAND;if(f&((1u<<11)|(1u<<17)))return hx::S_GRASS;if(f&(1u<<18))return hx::S_TILE;if(f&(1u<<10))return hx::S_CONCRETE;
    return hx::S_DEFAULT;
}
// Wheel grit for the road texture: gravel/dust/sand spray = 1, grass = 0.35 (flags2 bits 2, 4, 5 and 1).
float wheelGrit(unsigned id){if(id>=SURFACE_COUNT)return 0;uint32_t f=mem<uint32_t>(SURFACE_INFOS+0x98+id*12);return (f&0x34)?1.f:(f&0x2)?0.35f:0.f;}
// Pan (-1 left .. +1 right) of a world point relative to where the camera looks.
float panOf(float dx,float dy){
    Vec3 f=mem<Vec3>(CAMERA_FORWARD);float rx=f.y,ry=-f.x,rl=std::sqrt(rx*rx+ry*ry),d=std::sqrt(dx*dx+dy*dy);   // right = forward x up
    return rl>1e-3f&&d>0.5f?std::clamp((dx*rx+dy*ry)/(rl*d),-1.f,1.f)*0.8f:0.f;
}
ds::Rumble rumble;unsigned long long lastSampleAt=0;

const char* blockReason(){
    if(mem<BYTE>(MENU_ACTIVE))return "menu";
    if(mem<BYTE>(USER_PAUSE))return "pause";
    if(mem<BYTE>(CODE_PAUSE))return "code pause";
    if(mem<BYTE>(CUTSCENE_RUNNING))return "cutscene";
    if(mem<uint16_t>(PAD0_DISABLE_CONTROLS))return "player controls disabled (script / Photo Mode)";
    // The Photo Mode clears m_CodePause while it renders (time step 0.001) and in its slow motion (time scale < 1).
    if(mem<float>(TIME_SCALE)<0.9f)return "slow motion (Photo Mode)";
    if(mem<float>(TIME_STEP)<0.01f)return "time frozen (Photo Mode)";
    if(!plausible(mem<uintptr_t>(PLAYER)))return "no player";
    if(!cfg.allowBackground&&!gameFocused())return "game not focused";
    return nullptr;
}
Vec3 entityPos(uintptr_t e){uintptr_t m=mem<uintptr_t>(e+PED_MATRIX);return plausible(m)?mem<Vec3>(m+0x30):mem<Vec3>(e+PED_POS);}
unsigned roughness(unsigned surface){return surface<SURFACE_COUNT?(mem<uint32_t>(SURFACE_INFOS+0x94+surface*12)>>23)&3:0;}
bool isAutomobile(int sub){return sub==0||sub==1||sub==2||sub==3||sub==4||sub==7||sub==8||sub==11;}
bool isBike(int sub){return sub==9||sub==10;}
float scaleV(float v){return v*cfg.vibration/100.f;}

void resync(uintptr_t ped){
    tr.ped=ped;tr.slot=std::min<int>(mem<BYTE>(ped+PED_WEAPON_SLOT),12);uintptr_t w=ped+PED_WEAPONS+tr.slot*WEAPON_SIZE;
    tr.type=mem<unsigned>(w);tr.state=mem<unsigned>(w+4);tr.total=mem<unsigned>(w+0xC);tr.nextShot=mem<unsigned>(w+0x10);
    tr.hp=mem<float>(ped+PED_HEALTH)+mem<float>(ped+PED_ARMOUR);tr.veh=0;tr.vhp=0;tr.airSince=0;
    for(int i=0;i<EXPLOSION_COUNT;++i)tr.explosion[i]=mem<unsigned>(EXPLOSIONS+i*EXPLOSION_SIZE+0x2C);
    for(bool& m:tr.projMolotov)m=false;
    tr.init=true;
}

ds::OutState computeOut(unsigned long long now,int& fp){
    ds::OutState o=neutralOut();fp=0;
    float gl=cfg.gameRumble?static_cast<float>(gameRumbleL):0.f,gr=cfg.gameRumble?static_cast<float>(gameRumbleR):0.f;
    const char* block=gameOk?blockReason():"unsupported executable";
    uintptr_t ped=block?0:mem<uintptr_t>(PLAYER);
    float health=ped?mem<float>(ped+PED_HEALTH):0;
    bool wasted=ped&&health<=0.f;
    // Hysteresis: the Photo Mode clears m_CodePause only while it renders each frame, so a block must stay clear for
    // 500 ms before the effects come back (v1.1 flipped on/off 4 times a second there: blinking light, trigger pulses).
    static unsigned long long lastBlockedAt=0;
    const char* why=block?block:wasted?"wasted":nullptr;
    if(why)lastBlockedAt=now;else if(*tr.block&&now-lastBlockedAt<500)why=tr.block;
    if(why){
        if(std::strcmp(why,tr.block)){logf("effects off: %s",why);tr.block=why;tr.profile="";tr.base="";}
        tr.init=false;
        if(wasted){float l,r;rumble.level(now,l,r);gl=std::max(gl,l);gr=std::max(gr,r);}else rumble.clear();
        if(block&&!std::strcmp(block,"game not focused"))gl=gr=0;
        hapSetLayers(hx::Layers{},now);tr.footAirSince=0;
        o.rumL=static_cast<uint8_t>(std::clamp(scaleV(gl),0.f,255.f));o.rumR=static_cast<uint8_t>(std::clamp(scaleV(gr),0.f,255.f));
        return o;
    }
    if(*tr.block){logf("effects on (was: %s)",tr.block);tr.block="";}
    if(!tr.init||tr.ped!=ped)resync(ped);
    unsigned gameTime=mem<unsigned>(GAME_TIME_MS);
    float armour=mem<float>(ped+PED_ARMOUR),hp=health+armour;
    float maxHealth=std::max(1.f,static_cast<float>(mem<BYTE>(PLAYER_MAX_HEALTH))),maxArmour=std::max(1.f,static_cast<float>(mem<BYTE>(PLAYER_MAX_ARMOUR)));
    Vec3 pos=entityPos(ped);
    float contL=0,contR=0;
    hx::Layers ly;   // HD haptics: continuous layers built below, sent at the end
    float dt=tr.lastNow&&now>tr.lastNow?std::min(0.1f,(now-tr.lastNow)/1000.f):0.f;tr.lastNow=now;

    // --- weapon ---
    int slot=mem<BYTE>(ped+PED_WEAPON_SLOT);if(slot>12)slot=0;
    uintptr_t w=ped+PED_WEAPONS+slot*WEAPON_SIZE;
    unsigned type=mem<unsigned>(w),state=mem<unsigned>(w+4),clip=mem<unsigned>(w+8),total=mem<unsigned>(w+0xC),nextShot=mem<unsigned>(w+0x10);
    const ds::WeaponProfile& prof=ds::weapon_profile(type);
    bool sameGun=slot==tr.slot&&type==tr.type;
    unsigned shots=0;
    if(sameGun&&total<tr.total&&tr.total-total<=60)shots=tr.total-total;
    else if(sameGun&&state==1&&nextShot>tr.nextShot)shots=1;   // infinite-ammo cheats keep the total
    if(shots&&prof.kind!=ds::K_NONE){
        tr.lastShotAt=now;
        if(cfg.shots&&prof.rumMs)rumble.add(prof.rumL,prof.rumR,prof.rumMs,now);
        float clipGain=1.f;const hx::Clip* clip=cfg.shots?shotClip(type,clipGain):nullptr;   // the gun's own sound, if its EarShot folder was found
        if(clip){hapClip(clip,clipGain,0.35f);hap(hx::weapon_hit(type),0.4f,0.35f);}   // the sound gives the body, a light synth hit the crisp attack
        else if(cfg.shots)hap(hx::weapon_hit(type),1.f,0.35f);   // the gun sits in the right hand
        if(cfg.shots&&prof.kind==ds::K_BOLT)hapRecipe(hx::bolt_open(),1.f,0.25f);   // bolt opens a quarter second after the shot
        if(cfg.shots&&(type==16||type==17||type==18||type==39))hap(hx::throw_swish(),1.f,0.35f);   // grenade, tear gas, molotov, satchel leave the hand
        event(EV_SHOT,"%s x%u (clip %u, total %u)",prof.name,shots,clip,total);
    }
    if(cfg.shots&&state==1&&!prof.rumMs&&prof.kind!=ds::K_NONE){contL=std::max(contL,prof.rumL*0.8f);contR=std::max(contR,prof.rumR*0.8f);}   // continuous fire
    if(cfg.shots){   // HD loops while firing (minigun spin, flames, sprays, chainsaw) and reload clicks
        if(state==1&&type==38){ly.loopHz=70;ly.loopAmp=0.35f;ly.loopNoise=0.3f;}
        else if(state==1&&type==37){ly.loopHz=60;ly.loopAmp=0.30f;ly.loopNoise=0.85f;}
        else if(state==1&&(type==41||type==42)){ly.loopHz=90;ly.loopAmp=0.15f;ly.loopNoise=0.95f;}
        else if(type==9){ly.loopHz=state==1?95.f:55.f;ly.loopAmp=state==1?0.45f:0.12f;ly.loopNoise=state==1?0.35f:0.2f;}
        if(sameGun&&state==2&&tr.state!=2){if(const hx::Clip* c=reloadClip(type,false))hapClip(c,0.6f,0.35f);else hap(hx::reload_click(false),1.f,0.35f);}
        else if(sameGun&&tr.state==2&&state==0){if(const hx::Clip* c=reloadClip(type,true))hapClip(c,0.6f,0.35f);else hap(hx::reload_click(true),1.f,0.35f);}
    }
    if(state==4&&tr.state!=4&&cfg.shots){rumble.add(150,210,90,now);hap(hx::melee_hit(),1.f,0.3f);event(EV_MELEE,"%s hit",type<=15?"melee":prof.name);}
    bool cycling=sameGun&&prof.kind==ds::K_BOLT&&nextShot>gameTime&&nextShot-gameTime<5000;   // bolt still working
    if(tr.cycling&&!cycling&&sameGun&&cfg.shots)hapRecipe(hx::bolt_close());tr.cycling=cycling;
    tr.slot=slot;tr.type=type;tr.state=state;tr.total=total;tr.nextShot=nextShot;

    // --- damage ---
    if(hp<tr.hp-0.5f){float d=tr.hp-hp,k=std::clamp(d/40.f,0.25f,1.f);if(cfg.damage)rumble.add(255*k,150*k,static_cast<unsigned>(std::min(450.f,120+d*4)),now);if(cfg.damage)hap(hx::damage_hit(k));
        event(EV_DAMAGE,"-%.1f (health %.0f armour %.0f)",d,health,armour);}
    tr.hp=hp;

    // --- explosions ---
    for(int i=0;i<EXPLOSION_COUNT;++i){
        uintptr_t e=EXPLOSIONS+i*EXPLOSION_SIZE;unsigned created=mem<unsigned>(e+0x2C);
        if(created==tr.explosion[i])continue;tr.explosion[i]=created;
        if(!created)continue;   // m_nCreatedTime is not CTimer game time in this build (it runs 2x), so any change is a new explosion
        int etype=mem<int>(e);float range=etype==1?40.f:70.f;   // 1 = molotov (eExplosionType)
        Vec3 ep=mem<Vec3>(e+4);float dx=ep.x-pos.x,dy=ep.y-pos.y,dz=ep.z-pos.z,dist=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(dist<range){float k=(1-dist/range)*(1-dist/range);
            if(etype==1){if(cfg.explosions&&now-tr.lastGlassAt>400){tr.lastGlassAt=now;rumble.add(80*k,220*k,180,now);hapRecipe(hx::glass_break(std::max(k,0.35f),panOf(dx,dy),hapRng));}
                event(EV_EXPLOSION,"molotov breaks at %.1f m (strength %.2f)",dist,k);}
            else{if(cfg.explosions){rumble.add(255*k,200*k,static_cast<unsigned>(250+400*k),now);hap(hx::explosion_hit(k),1.f,panOf(dx,dy));}
                event(EV_EXPLOSION,"type %d at %.1f m (strength %.2f)",etype,dist,k);}}
        else event(EV_EXPLOSION,"type %d at %.0f m (too far to feel)",etype,dist);
    }

    // --- molotovs in flight: when one disappears it hit something, so the bottle breaks there ---
    for(int i=0;i<PROJECTILE_COUNT;++i){
        uintptr_t info=PROJECTILE_INFO+i*PROJECTILE_INFO_SIZE;bool active=mem<BYTE>(info+0x10)!=0;unsigned wt=mem<unsigned>(info);
        if(active&&wt==18){uintptr_t obj=mem<uintptr_t>(PROJECTILES+i*4);tr.projPos[i]=plausible(obj)?entityPos(obj):mem<Vec3>(info+0x14);tr.projMolotov[i]=true;}
        else if(tr.projMolotov[i]){
            tr.projMolotov[i]=false;Vec3 p=tr.projPos[i];float dx=p.x-pos.x,dy=p.y-pos.y,dz=p.z-pos.z,dist=std::sqrt(dx*dx+dy*dy+dz*dz);
            if(dist<45.f&&now-tr.lastGlassAt>400){tr.lastGlassAt=now;float k=std::max(0.6f,1-dist/45.f);
                if(cfg.explosions){rumble.add(80*k,220*k,180,now);hapRecipe(hx::glass_break(k,panOf(dx,dy),hapRng));}
                event(EV_EXPLOSION,"molotov breaks at %.1f m (projectile, strength %.2f)",dist,k);}
        }
    }

    // --- vehicle ---
    uintptr_t veh=(mem<BYTE>(ped+PED_FLAGS2)&1)?mem<uintptr_t>(ped+PED_VEHICLE):0;if(!plausible(veh))veh=0;
    bool driver=veh&&mem<uintptr_t>(veh+VEH_DRIVER)==ped;
    ds::VehicleState vs;int sub=-1;
    if(driver){
        sub=mem<int>(veh+VEH_SUBCLASS);vs.kind=ds::vehicle_kind(sub);
        Vec3 v=mem<Vec3>(veh+VEH_MOVE_SPEED);vs.kmh=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z)*180.f;
        vs.health=mem<float>(veh+VEH_HEALTH);vs.braking=mem<float>(veh+VEH_BRAKE)>0.25f;vs.handbrake=(mem<BYTE>(veh+VEH_FLAGS)&0x20)!=0;
        float gas=mem<float>(veh+VEH_GAS);
        unsigned rough=0;float bumps=0,grit=0;int onGround=-1;
        if(isAutomobile(sub)&&vs.kind!=ds::V_AIR){
            for(int i=0;i<4;++i){int ws=mem<int>(veh+CAR_WHEEL_STATE+i*4);vs.skidding|=ws==2;vs.spinning|=ws==1&&gas>0.3f;
                float c=mem<float>(veh+CAR_SUSP+i*4),p=mem<float>(veh+CAR_SUSP_PREV+i*4);
                if(c<1.f){unsigned id=mem<BYTE>(veh+CAR_COLPOINTS+i*COLPOINT_SIZE+COLPOINT_SURFACE_B);rough=std::max(rough,roughness(id));grit=std::max(grit,wheelGrit(id));}
                if(c<1.f||p<1.f)bumps+=std::fabs(c-p);}
            onGround=mem<BYTE>(veh+CAR_ON_GROUND);
        }else if(isBike(sub)){
            for(int i=0;i<2;++i){int ws=mem<int>(veh+BIKE_WHEEL_STATE+i*4);vs.skidding|=ws==2;vs.spinning|=ws==1&&gas>0.3f;}
            for(int i=0;i<4;++i){float c=mem<float>(veh+BIKE_RATIOS+i*4);
                if(c<1.f){unsigned id=mem<BYTE>(veh+BIKE_COLPOINTS+i*COLPOINT_SIZE+COLPOINT_SURFACE_B);rough=std::max(rough,roughness(id));grit=std::max(grit,wheelGrit(id));}
                if(veh==tr.veh&&(c<1.f||tr.bikePrev[i]<1.f))bumps+=std::fabs(c-tr.bikePrev[i]);tr.bikePrev[i]=c;}
            onGround=mem<BYTE>(veh+BIKE_ON_GROUND);
        }
        vs.inAir=onGround==0;
        if(onGround>=0){
            if(vs.inAir){if(!tr.airSince)tr.airSince=now;}
            else if(tr.airSince){unsigned long long air=now-tr.airSince;tr.airSince=0;
                if(air>350&&veh==tr.veh){float k=std::clamp(air/1500.f,0.3f,1.f);if(cfg.terrain)rumble.add(220*k,240*k,static_cast<unsigned>(140+160*k),now);if(cfg.terrain)hap(hx::landing_hit(k));
                    event(EV_LANDING,"after %.2f s in the air",air/1000.0);}}
        }
        if(cfg.terrain&&!vs.inAir&&vs.kmh>5){float sp=std::min(vs.kmh/70.f,1.f);contL=std::max(contL,rough*12*sp);contR=std::max(contR,rough*22*sp);
            if(bumps>0.04f){float k=std::clamp(bumps*2.5f,0.f,1.f);rumble.add(45*k,110*k,70,now);hap(hx::bump_hit(k),cfg.drivingVibration/100.f);}   // v1.0 (0.02, 60/160) felt too hard
            if(vs.skidding&&vs.kmh>20){contL=std::max(contL,20.f);contR=std::max(contR,45.f);}}
        if(veh==tr.veh&&vs.health<tr.vhp-3){float d=tr.vhp-vs.health,k=std::clamp(d/150.f,0.2f,1.f);
            // Zone of the hit: CPhysical::m_vecLastCollisionPosn (+0xEC) in the vehicle's frame (matrix right +0, forward +0x10).
            hx::Zone zone=hx::Z_FRONT;float zpan=0;const char* zname="front";
            Vec3 cp=mem<Vec3>(veh+VEH_COLLISION_POS);uintptr_t vm=mem<uintptr_t>(veh+PED_MATRIX);
            if(plausible(vm)){Vec3 r=mem<Vec3>(vm),f=mem<Vec3>(vm+0x10),p=mem<Vec3>(vm+0x30);float dx=cp.x-p.x,dy=cp.y-p.y,dz=cp.z-p.z;
                if(dx*dx+dy*dy+dz*dz<100.f){float lx=dx*r.x+dy*r.y+dz*r.z,ly=dx*f.x+dy*f.y+dz*f.z;
                    if(std::fabs(ly)>=std::fabs(lx)*1.2f){zone=ly>0?hx::Z_FRONT:hx::Z_REAR;zpan=std::clamp(lx/1.5f,-0.6f,0.6f);zname=ly>0?"front":"rear";}
                    else{zone=hx::Z_SIDE;zpan=lx>0?0.9f:-0.9f;zname=lx>0?"right side":"left side";}}}
            if(cfg.crashes){rumble.add(255*k,200*k,static_cast<unsigned>(std::min(500.f,150+d*1.5f)),now);hapRecipe(hx::crash(k,zone,zpan));}
            event(EV_CRASH,"vehicle -%.0f health (%.0f km/h, now %.0f, %s)",d,vs.kmh,vs.health,zname);}
        if(veh!=tr.veh||sub!=tr.subClass)logf("driving %s (model %d, type %d)",vs.kind==ds::V_CAR?"car":vs.kind==ds::V_BIKE?"bike":vs.kind==ds::V_BMX?"BMX":
            vs.kind==ds::V_BOAT?"boat":vs.kind==ds::V_AIR?"aircraft":vs.kind==ds::V_TRAIN?"train":"other",mem<int16_t>(veh+0x22),sub);
        // HD layers: engine pitch from the audio revs, road texture by surface, tyre screech; all at the driving volume
        bool engineOn=(mem<BYTE>(veh+VEH_FLAGS)&0x10)!=0;float revs=std::clamp(vs.kmh/150.f+std::fabs(gas)*0.3f,0.f,1.f);
        if(isAutomobile(sub)&&vs.kind!=ds::V_AIR)revs=mem<float>(veh+CAR_AUDIO_REVS);else if(sub==9)revs=mem<float>(veh+BIKE_AUDIO_REVS);
        if(engineOn&&vs.kind!=ds::V_BMX&&vs.kind!=ds::V_TRAIN&&vs.kind!=ds::V_NONE)hx::engine_layer(vs.kind==ds::V_BIKE?2:1,revs,gas,mem<float>(veh+VEH_MASS)>3500.f,ly);
        if(cfg.terrain&&!vs.inAir&&vs.kmh>3&&onGround!=-1){ly.terrainAmp=(0.03f+0.09f*rough+0.06f*grit)*std::min(vs.kmh/60.f,1.f);ly.terrainGrit=grit;
            if(vs.skidding&&vs.kmh>20)ly.skidAmp=0.30f;else if(vs.spinning)ly.skidAmp=0.22f;}
        float drive=cfg.drivingVibration/100.f;ly.engineAmp*=drive;ly.terrainAmp*=drive;ly.skidAmp*=drive;
        tr.vhp=vs.health;
    }else if(tr.driver)log("on foot");
    tr.veh=driver?veh:0;tr.driver=driver;tr.subClass=sub;playerDriving=driver;

    // --- HD footsteps by surface and on-foot landings ---
    // Timing copies CPed::PlayFootSteps (gta-reversed): on the walk/run/sprint animation one foot lands at T/15 and the
    // other at T/2 + T/15 of the cycle, so the haptic step meets the game's own step sound. Other moves use the speed.
    if(!veh){
        Vec3 mv=mem<Vec3>(ped+PED_MOVE_SPEED);float v=std::sqrt(mv.x*mv.x+mv.y*mv.y)*50.f;
        bool standing=(mem<BYTE>(ped+PED_FLAGS1)&1)!=0,air=(mem<BYTE>(ped+PED_FLAGS2)&2)!=0;   // bIsStanding, bIsInTheAir
        int stepFoot=-1;float stepGain=std::clamp(0.55f+0.08f*v,0.55f,1.f);const char* source="speed";
        uintptr_t clump=mem<uintptr_t>(ped+PED_CLUMP),assoc=0;
        if(plausible(clump)){uintptr_t other=0;float blend=0;assoc=reinterpret_cast<GetMainAssocFn>(FN_GET_MAIN_ASSOCIATION)(clump,&other,&blend);}
        bool animDriven=false;
        if(standing&&plausible(assoc)){
            int id=mem<int16_t>(assoc+ASSOC_ANIM_ID);uintptr_t hier=mem<uintptr_t>(assoc+ASSOC_HIERARCHY);
            float total=plausible(hier)?mem<float>(hier+HIER_TOTAL_TIME):0.f,cur=mem<float>(assoc+ASSOC_CURRENT_TIME);
            if(id>=0&&id<=2&&total>0.1f&&mem<float>(assoc+ASSOC_BLEND)>=0.5f&&v>0.3f){   // ANIM_ID_WALK / RUN / SPRINT
                animDriven=true;source="anim";stepGain=id==0?0.55f:id==1?0.8f:1.f;
                if(assoc==tr.animAssoc){float prev=tr.animPrev,a=total/15.f,b=total/2.f+total/15.f;
                    auto crossed=[&](float th){return cur>=prev?(prev<th&&cur>=th):(prev<th||cur>=th);};   // handles the loop wrap
                    if(crossed(a))stepFoot=0;else if(crossed(b))stepFoot=1;}
                tr.animAssoc=assoc;tr.animPrev=cur;tr.stepPhase=0.5f;
            }
        }
        if(!animDriven){
            tr.animAssoc=0;
            if(standing&&v>0.6f){tr.stepPhase+=hx::step_rate(v)*dt;if(tr.stepPhase>=1.f){tr.stepPhase-=1.f;tr.stepLeft=!tr.stepLeft;stepFoot=tr.stepLeft?0:1;}}
            else tr.stepPhase=0.5f;   // the first step lands half a stride after starting to move
        }
        if(stepFoot>=0){unsigned id=mem<BYTE>(ped+PED_CONTACT_SURFACE);hx::Surface s=footSurface(id);
            if(cfg.footsteps)hapRecipe(hx::footstep(s,stepGain,stepFoot==0?-0.45f:0.45f,hapRng));
            event(EV_STEP,"%s %s foot (surface %u, %.1f m/s, %s)",hx::surface_name(s),stepFoot==0?"left":"right",id,v,source);}
        if(air){if(!tr.footAirSince)tr.footAirSince=now;}
        else if(tr.footAirSince){unsigned long long t=now-tr.footAirSince;tr.footAirSince=0;
            if(t>300){float k=std::clamp(t/1200.f,0.3f,1.f);if(cfg.terrain)hap(hx::landing_hit(k));event(EV_LANDING,"on foot after %.2f s in the air",t/1000.0);}}
    }else{tr.footAirSince=0;tr.animAssoc=0;}

    // --- triggers ---
    ds::TriggerPair tp;tp.l2=tp.r2=ds::fx_off();tp.name="off";const char* base="off";
    if(!cfg.triggers||userTriggersOff)tp.name=base=userTriggersOff?"off (Mute)":"off (INI)";
    else if(driver){base=vs.kind==ds::V_CAR?"car":vs.kind==ds::V_BIKE?"bike":vs.kind==ds::V_BMX?"bmx":vs.kind==ds::V_BOAT?"boat":vs.kind==ds::V_AIR?"aircraft":"vehicle (off)";
        if(cfg.drivingTriggers)tp=ds::vehicle_triggers(vs,cfg.drivingStrength,cfg.speedResponse,static_cast<float>(cfg.speedKmh));}
    else if(cfg.weaponTriggers){
        ds::WeaponState ws;ws.type=type;ws.hasAmmo=clip>0||total>0;ws.reloading=state==2;ws.firing=state==1;ws.cycling=cycling;ws.sinceShotMs=tr.lastShotAt?now-tr.lastShotAt:100000;
        tp=ds::weapon_triggers(ws,cfg.weaponStrength,cfg.fireAtClick);base=prof.name;
    }
    o.l2=tp.l2;o.r2=tp.r2;fp=tp.firePoint;
    // The weapon/vehicle profile is always logged; short states (auto-fire, kick, ABS, wheelspin) go through the event limiter.
    if(std::strcmp(base,tr.base)){logf("triggers: %s -> L2 %s, R2 %s, fire point %d",base,ds::fx_name(tp.l2),ds::fx_name(tp.r2),fp);tr.base=base;tr.profile=tp.name;}
    else if(std::strcmp(tp.name,tr.profile)){event(EV_TRIGGER,"triggers: %s (L2 %s, R2 %s)",tp.name,ds::fx_name(tp.l2),ds::fx_name(tp.r2));tr.profile=tp.name;}

    // --- lights ---
    if(cfg.lightbar){
        ds::Rgb c=cfg.menu;uint8_t leds=0x04;
        if(driver&&cfg.speedometer){float frac=vs.kmh/cfg.speedKmh;c=ds::speed_color(frac);leds=ds::gauge_leds(frac);}
        else{if(cfg.healthColor)c=ds::health_color(health/maxHealth,now);if(cfg.armourLeds)leds=ds::gauge_leds(armour/maxArmour);}
        uintptr_t wanted=mem<uintptr_t>(PLAYER_WANTED);ds::Rgb flash;
        if(cfg.policeFlashes&&plausible(wanted)&&ds::police_flash(mem<unsigned>(wanted+0x2C),now,flash))c=flash;
        o.light=ds::dim(c,cfg.brightness/100.f);o.leds=leds;
    }

    // --- HD layers: the game's own rumble as low/high sines, then everything at the master volume ---
    ly.rumbleLow=gl/255.f*0.55f;ly.rumbleHigh=gr/255.f*0.45f;
    {float m=hdMaster();ly.engineAmp*=m;ly.terrainAmp*=m;ly.skidAmp*=m;ly.loopAmp*=m;ly.rumbleLow*=m;ly.rumbleHigh*=m;}
    hapSetLayers(ly,now);

    // --- rumble ---
    float l,r;rumble.level(now,l,r);l=std::max({l,contL,gl});r=std::max({r,contR,gr});
    if(driver){float k=cfg.drivingVibration/100.f;l*=k;r*=k;}   // driving has its own volume (game rumble included)
    o.rumL=static_cast<uint8_t>(std::clamp(scaleV(l),0.f,255.f));o.rumR=static_cast<uint8_t>(std::clamp(scaleV(r),0.f,255.f));
    return o;
}
// ---------- Xbox mode: no Sony pad, a real XInput pad in slot 0 ----------
// The same effects as classic rumble on the DualSense (shots, hits, explosions, crashes, terrain) plus the game's own
// rumble go to that pad: Xbox controllers, generic XInput pads, or a DualShock 3 through DsHidMini in XInput mode.
std::atomic<unsigned long long> xboxSentAt{0};std::atomic<unsigned> xboxSent{0};
template<class Fn> Fn systemFn(int f){for(const HookedDll& h:dlls)if(h.original[f])return reinterpret_cast<Fn>(h.original[f]);return nullptr;}
void xboxSend(uint8_t l,uint8_t r){
    auto set=systemFn<SetStateFn>(F_SET);if(!set)return;
    XINPUT_VIBRATION v{static_cast<WORD>(l*257),static_cast<WORD>(r*257)};set(0,&v);
    xboxSent=(static_cast<unsigned>(l)<<8)|r;xboxSentAt=GetTickCount64();
}
void xboxStop(){if(xboxActive&&xboxSent)xboxSend(0,0);}
void xboxTick(unsigned long long now){
    static unsigned long long nextProbe=0;static bool present=false;
    if(!cfg.xboxRumble||dryRun){if(xboxActive){xboxStop();xboxActive=false;}return;}
    if(now>=nextProbe){
        nextProbe=now+1000;auto get=systemFn<GetStateFn>(F_GET);XINPUT_STATE st{};bool p=get&&get(0,&st)==ERROR_SUCCESS;
        if(p!=present){present=p;log(p?"Xbox mode: XInput pad in slot 0 and no Sony pad, the mod's rumble effects go to it":"Xbox mode: the XInput pad in slot 0 is gone");}
    }
    if(!present){xboxActive=false;return;}
    xboxActive=true;
    int fp=0;ds::OutState o=computeOut(now,fp);
    if(!cfg.allowBackground&&!gameFocused())o.rumL=o.rumR=0;
    unsigned v=(static_cast<unsigned>(o.rumL)<<8)|o.rumR;
    if(v!=xboxSent||now-xboxSentAt>500)xboxSend(o.rumL,o.rumR);
}
void gameTick(){
    if(!padConnected){
        unsigned long long now=GetTickCount64();if(now-lastSampleAt<5)return;lastSampleAt=now;
        xboxTick(now);return;
    }
    if(xboxActive){xboxStop();xboxActive=false;}   // a Sony pad took slot 0: the Xbox pad goes quiet
    unsigned long long now=GetTickCount64();if(now-lastSampleAt<5)return;lastSampleAt=now;
    int fp=0;ds::OutState o=computeOut(now,fp);
    if(isDs4())fp=0;   // no trigger click on a DualShock 4, so the game reads the trigger as usual
    if(menuOpen){ds::TriggerPair t;if(!menuTriggerPreview(t)){t.l2=t.r2=ds::fx_off();}o.l2=t.l2;o.r2=t.r2;fp=0;}
    if(now<pulseUntil)o.rumL=o.rumR=static_cast<uint8_t>(cfg.vibration*255/100);
    EnterCriticalSection(&stateLock);desired=o;desiredAt=now;firePoint=fp;LeaveCriticalSection(&stateLock);
}

// ---------- HID device ----------
struct Device{
    HANDLE h=INVALID_HANDLE_VALUE;USHORT inLen=0,outLen=0;bool vibrationV2=false,setupLights=true,bt=false;uint8_t seq=0;
    OVERLAPPED rov{},wov{};BYTE in[1024]{};bool readPending=false;unsigned writeErrors=0;
} dev;
unsigned long long reportsIn=0,reportsOut=0,writeFailures=0;unsigned openFailures=0;bool loggedSimple=false;

void closeDevice(const char* why){
    if(dev.h==INVALID_HANDLE_VALUE)return;
    CancelIoEx(dev.h,nullptr);if(dev.readPending){DWORD got=0;GetOverlappedResult(dev.h,&dev.rov,&got,TRUE);}   // the OVERLAPPED is reused on reconnect
    CloseHandle(dev.h);dev.h=INVALID_HANDLE_VALUE;dev.readPending=false;
    padConnected=false;gameRumbleL=gameRumbleR=0;
    EnterCriticalSection(&stateLock);pad=ds::XPad{};firePoint=0;LeaveCriticalSection(&stateLock);
    logf("%s disconnected (%s); XInput falls back to the system, waiting for the controller",padName(),why);
}
bool tryOpen(){
    GUID guid;HidD_GetHidGuid(&guid);
    HDEVINFO set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE)return false;
    bool opened=false;SP_DEVICE_INTERFACE_DATA data{};data.cbSize=sizeof data;
    // Pass 0 looks for a DualSense, pass 1 for a DualShock 4, so a DualSense wins when both are plugged in ([General] Controller).
    for(int pass=0;pass<2&&!opened;++pass){
    if((pass==0&&cfg.controller==2)||(pass==1&&cfg.controller==1))continue;
    for(DWORD i=0;!opened&&SetupDiEnumDeviceInterfaces(set,nullptr,&guid,i,&data);++i){
        DWORD need=0;SetupDiGetDeviceInterfaceDetailW(set,&data,nullptr,0,&need,nullptr);if(!need||need>4096)continue;
        BYTE buffer[4096];auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer);detail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&data,detail,need,nullptr,nullptr))continue;
        // Attributes need no access rights, so other HID devices are never opened for reading.
        HANDLE probe=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(probe==INVALID_HANDLE_VALUE)continue;
        HIDD_ATTRIBUTES at{};at.Size=sizeof at;bool match=HidD_GetAttributes(probe,&at)&&at.VendorID==0x054C&&(pass==0?(at.ProductID==0x0CE6||at.ProductID==0x0DF2):   // DualSense, DualSense Edge
            (at.ProductID==0x05C4||at.ProductID==0x09CC||at.ProductID==0x0BA0));   // DualShock 4 v1, v2, Sony wireless adapter
        CloseHandle(probe);if(!match)continue;
        HANDLE h=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(h==INVALID_HANDLE_VALUE){if(openFailures++<5)logf("Sony controller found but CreateFile failed (error %lu): another program may own it exclusively (DS4Windows/DSX hide mode)",GetLastError());continue;}
        PHIDP_PREPARSED_DATA pre=nullptr;HIDP_CAPS caps{};
        if(HidD_GetPreparsedData(h,&pre)){HidP_GetCaps(pre,&caps);HidD_FreePreparsedData(pre);}
        // USB pads have 64-byte input reports; over Bluetooth Windows reports the largest of the pad's BT reports (78 or more).
        bool bt=caps.InputReportByteLength!=64;
        if(caps.InputReportByteLength<10||caps.InputReportByteLength>sizeof dev.in||caps.OutputReportByteLength>1024||(bt&&caps.OutputReportByteLength<ds::BT_OUTPUT_SIZE)){
            if(openFailures++<5)logf("Sony controller with unexpected report sizes (input %u, output %u), ignoring it",caps.InputReportByteLength,caps.OutputReportByteLength);
            CloseHandle(h);continue;
        }
        bool ds4=at.ProductID==0x05C4||at.ProductID==0x09CC||at.ProductID==0x0BA0;padModel=ds4?2:at.ProductID==0x0DF2?1:0;
        dev.h=h;dev.bt=bt;padBluetooth=bt;dev.seq=0;dev.inLen=caps.InputReportByteLength;
        dev.outLen=std::max<USHORT>(caps.OutputReportByteLength,static_cast<USHORT>(bt?ds::BT_OUTPUT_SIZE:ds4?ds::DS4_OUTPUT_SIZE:ds::USB_OUTPUT_SIZE));
        dev.vibrationV2=false;dev.setupLights=!ds4;dev.writeErrors=0;loggedSimple=false;
        if(bt){   // reading the calibration report switches the pad from its simple report to the full one (0x31 / 0x11)
            BYTE cal[1024]{};cal[0]=0x05;ULONG calLen=std::min<ULONG>(caps.FeatureReportByteLength,sizeof cal);
            bool ok=calLen>=41&&HidD_GetFeature(h,cal,calLen);
            logf("Bluetooth: calibration report %s; HD haptics need the USB cable (classic rumble over Bluetooth)",ok?"read, full input reports on":"unavailable, staying on simple reports");
        }
        if(!ds4){
            BYTE fw[256]{};fw[0]=0x20;unsigned update=0;ULONG fwLen=std::min<ULONG>(caps.FeatureReportByteLength,sizeof fw);
            if(fwLen>=64&&HidD_GetFeature(h,fw,fwLen)){update=fw[44]|(fw[45]<<8);dev.vibrationV2=update>=0x0215||at.ProductID==0x0DF2;   // Linux: DS_FEATURE_VERSION(2,21); the Edge always uses v2
                logf("firmware: hardware %08lX, firmware %08lX, update %04X -> %s rumble",static_cast<unsigned long>(fw[24]|(fw[25]<<8)|(fw[26]<<16)|(static_cast<unsigned>(fw[27])<<24)),
                    static_cast<unsigned long>(fw[28]|(fw[29]<<8)|(fw[30]<<16)|(static_cast<unsigned>(fw[31])<<24)),update,dev.vibrationV2?"v2":"classic");}
            else log("firmware report unavailable; classic rumble");
        }
        char path[300];WideCharToMultiByte(CP_UTF8,0,detail->DevicePath,-1,path,sizeof path,nullptr,nullptr);
        logf("%s connected over %s: VID %04X PID %04X, input %u bytes, output %u bytes (%s)",padName(),bt?"Bluetooth":"USB",at.VendorID,at.ProductID,caps.InputReportByteLength,caps.OutputReportByteLength,path);
        opened=true;
    }
    }
    SetupDiDestroyDeviceInfoList(set);
    if(opened){padConnected=true;reportsIn=0;}
    return opened;
}
bool writeReport(const ds::OutState& s,bool release,OVERLAPPED& ov){
    BYTE usb[64]{},out[1024]{};
    if(isDs4())ds::build_ds4_output(s,usb);else ds::build_output(s,dev.vibrationV2,dev.setupLights,release,usb);
    if(!dev.bt)std::memcpy(out,usb,sizeof usb);
    else if(isDs4())ds::wrap_ds4_bt_output(usb,out);
    else ds::wrap_bt_output(usb,dev.seq++,out);   // Windows pads the write to the report size it reports; the pad reads its 78 bytes
    ResetEvent(ov.hEvent);DWORD done=0;
    BOOL ok=WriteFile(dev.h,out,dev.outLen,nullptr,&ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING){
        if(WaitForSingleObject(ov.hEvent,100)==WAIT_OBJECT_0)ok=GetOverlappedResult(dev.h,&ov,&done,FALSE);
        else{CancelIoEx(dev.h,&ov);GetOverlappedResult(dev.h,&ov,&done,TRUE);SetLastError(WAIT_TIMEOUT);ok=FALSE;}}   // cancel only this write, never the pending read
    if(!ok){++writeFailures;if(++dev.writeErrors==1||dev.writeErrors%50==0)logf("output report write failed (error %lu, %u in a row)",GetLastError(),dev.writeErrors);return false;}
    dev.writeErrors=0;dev.setupLights=false;++reportsOut;return true;
}
// Mute combos: tap = adaptive triggers on/off; hold Mute and press the D-pad: up/down = vibration +-10 %,
// right = open/close the menu (default shortcut). The lightbar turns white and the player LEDs show the new vibration
// level for 1.5 s; values are saved to the player's file in Documents.
// Street Talk SA sets bit 0x4000 of DisablePlayerControls while its settings menu is open (it navigates with the
// D-pad), so the D-pad combos that open this menu stay off until it clears the bit.
bool otherModMenu(){return gameOk&&(mem<uint16_t>(PAD0_DISABLE_CONTROLS)&0x4000);}
bool muteCombo=false;unsigned long long adjustAt=0,lastDownTap=0;int adjustLevel=0;bool adjustRumble=false;
void saveSetting(const wchar_t* section,const wchar_t* key,int value){if(!*userIni)return;wchar_t v[16];swprintf_s(v,L"%d",value);WritePrivateProfileStringW(section,key,v,userIni);}
void muteAdjust(const ds::Input& in){
    if(in.mute){
        bool fresh=in.hat!=8&&(!lastInput.mute||lastInput.hat!=in.hat);
        if(fresh&&(in.hat==0||in.hat==4)){
            muteCombo=true;int v=std::clamp(cfg.vibration+(in.hat==0?10:-10),0,100);cfg.vibration=v;saveSetting(L"Vibration",L"Vibration",v);
            adjustAt=GetTickCount64();adjustLevel=v;adjustRumble=true;logf("Mute + D-pad: vibration %d%%",v);hap(hx::Hit{150,60,0.25f,0.08f,0.3f,1.f});
        }else if(fresh&&in.hat==2){muteCombo=true;if(menuOpen||(menuOpenWith==0&&!otherModMenu()))menuToggleRequest=true;}
    }else if(lastInput.mute){
        if(!muteCombo&&cfg.muteToggles&&!isDs4()){bool off=!userTriggersOff;userTriggersOff=off;logf("Mute tapped: adaptive triggers %s",off?"OFF (mic LED on)":"ON");}
        muteCombo=false;
    }
}
void handleInput(const BYTE* r,DWORD n){
    ds::Input in;
    in.battery=lastInput.battery;in.charging=lastInput.charging;   // kept while the pad is still on its simple Bluetooth report
    bool ok=dev.bt?(isDs4()?ds::parse_ds4_bt_input(r,n,in):ds::parse_bt_input(r,n,in)):(isDs4()?ds::parse_ds4_usb_input(r,n,in):ds::parse_usb_input(r,n,in));
    if(!ok&&dev.bt&&ds::parse_bt_simple_input(r,n,in)){ok=true;if(!loggedSimple){loggedSimple=true;log("Bluetooth: simple input report (no battery or Mute until the full report starts)");}}
    if(!ok)return;
    if(isDs4()){in.mute=in.ps;in.ps=false;}   // no Mute button: PS is the combo key
    ++reportsIn;
    if(reportsIn==1)logf("first input report: battery %u%%%s",in.battery,in.charging?" (charging)":"");
    muteAdjust(in);
    if(otherModMenu())lastDownTap=0;   // Street Talk's menu owns the D-pad: re-arm the double tap
    else if(menuOpenWith==1&&!menuOpen&&!in.mute&&in.hat==4&&lastInput.hat!=4){   // double tap down opens the menu
        unsigned long long now=GetTickCount64();if(lastDownTap&&now-lastDownTap<350){menuToggleRequest=true;lastDownTap=0;}else lastDownTap=now;}
    ds::Input game=in;if(in.mute)game.hat=8;   // the D-pad belongs to the combo while Mute is held
    ds::XPad p=ds::to_xinput(game,cfg.touchpadAsBack,cfg.deadzone);
    EnterCriticalSection(&stateLock);
    if(std::memcmp(&p,&pad,sizeof p))++packet;
    pad=p;lastInput=in;   // the menu reads lastInput on the game thread
    LeaveCriticalSection(&stateLock);
}
void readStep(){
    if(!dev.readPending){
        ResetEvent(dev.rov.hEvent);
        if(ReadFile(dev.h,dev.in,dev.inLen,nullptr,&dev.rov)||GetLastError()==ERROR_IO_PENDING)dev.readPending=true;
        else{closeDevice("read failed");return;}
    }
    if(WaitForSingleObject(dev.rov.hEvent,8)!=WAIT_OBJECT_0)return;
    DWORD got=0;dev.readPending=false;
    if(!GetOverlappedResult(dev.h,&dev.rov,&got,FALSE)){char why[64];std::snprintf(why,sizeof why,"read error %lu",GetLastError());closeDevice(why);return;}
    handleInput(dev.in,got);
}

// Evidence for sound problems: OpenAL mods (EarShot) open the Windows default output when the game starts.
void logAudioDevices(){
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return;
    IMMDeviceEnumerator* e=nullptr;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&e)))){
        const PROPERTYKEY friendly{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};   // PKEY_Device_FriendlyName
        auto name=[&](IMMDevice* d,char* out,size_t size){out[0]=0;IPropertyStore* s=nullptr;
            if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&s))){PROPVARIANT v;PropVariantInit(&v);
                if(SUCCEEDED(s->GetValue(friendly,&v))&&v.vt==VT_LPWSTR)WideCharToMultiByte(CP_UTF8,0,v.pwszVal,-1,out,static_cast<int>(size),nullptr,nullptr);
                PropVariantClear(&v);s->Release();}};
        char text[160];IMMDevice* d=nullptr;
        if(SUCCEEDED(e->GetDefaultAudioEndpoint(eRender,eConsole,&d))){name(d,text,sizeof text);logf("audio: Windows default output is \"%s\"",text);d->Release();}
        IMMDeviceCollection* list=nullptr;UINT count=0;
        if(SUCCEEDED(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&list))&&SUCCEEDED(list->GetCount(&count)))
            for(UINT i=0;i<count;++i)if(SUCCEEDED(list->Item(i,&d))){name(d,text,sizeof text);logf("audio: output %u \"%s\"",i,text);d->Release();}
        if(list)list->Release();e->Release();
    }
    CoUninitialize();
}
DWORD WINAPI deviceThread(void*){
    logAudioDevices();
    {HANDLE a=CreateThread(nullptr,0,audioThread,nullptr,0,nullptr);if(a)CloseHandle(a);}   // HD haptics stream (waits for the pad)
    // The player's settings file lives next to the game saves (Documents\GTA San Andreas User Files); MO2 never touches it.
    {wchar_t docs[MAX_PATH]{};if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,0,docs))){wchar_t dir[MAX_PATH];swprintf_s(dir,L"%s\\GTA San Andreas User Files",docs);
        CreateDirectoryW(dir,nullptr);swprintf_s(userIni,L"%s\\Aleks DualSense SA.ini",dir);loadSettings();}}
    // Load the XInput DLLs the readers in this process use, so they are bridged before the first frame.
    for(const wchar_t* name:{L"xinput1_3.dll",L"xinput1_4.dll"})if(!GetModuleHandleW(name))LoadLibraryW(name);
    hookLoadedDlls();
    dev.rov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);dev.wov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(GetModuleHandleW(L"gameoverlayrenderer.dll"))log("note: Steam overlay is loaded. Turn Steam Input OFF for this game so it does not fight over the pad.");
    unsigned long long nextScan=0,nextDllScan=0,deviceSeenAt=0,lastSent=0,nextStatus=GetTickCount64()+30000,nextSlotCheck=0;
    ds::OutState sent;bool haveSent=false,warnedWatchdog=false,warnedSlot=false;
    while(!stopping){
        unsigned long long now=GetTickCount64();
        if(now>=nextDllScan){hookLoadedDlls();nextDllScan=now+(now-startTick<60000?500:3000);}
        if(gameOk&&!dryRun&&!presentHookTried){   // hook Present 3 s after the D3D9 device exists (the Photo Mode hooks it at the same time)
            if(!deviceSeenAt&&plausible(mem<uintptr_t>(0xC97C28)))deviceSeenAt=now;
            if(deviceSeenAt&&now-deviceSeenAt>3000)hookPresent();}
        {unsigned long long at=xboxSentAt;if(xboxSent&&at<now&&now-at>600)xboxSend(0,0);}   // the game stopped sampling (alt-tab, exit, crash): never leave the Xbox pad buzzing
        if(dev.h==INVALID_HANDLE_VALUE){
            haveSent=false;
            if(now>=nextScan){nextScan=now+1000;tryOpen();}
            if(dev.h==INVALID_HANDLE_VALUE){Sleep(50);continue;}
        }
        readStep();if(dev.h==INVALID_HANDLE_VALUE)continue;
        if(dev.setupLights&&!dryRun)writeReport(neutralOut(),false,dev.wov);   // first report only stops the firmware's blue connection animation
        // Output: what the game thread asked for, or neutral if the game stopped sampling (menu thread, exit, crash, alt-tab).
        ds::OutState want;unsigned long long at;
        EnterCriticalSection(&stateLock);want=desired;at=desiredAt;LeaveCriticalSection(&stateLock);
        now=GetTickCount64();unsigned long long age=at&&at<now?now-at:0;   // the game may have sampled during the read wait
        if(!at||age>350){
            if(at&&!warnedWatchdog){warnedWatchdog=true;logf("game stopped sampling (%llu ms): triggers released, rumble stopped",age);}
            ds::OutState n=neutralOut();n.light=at?want.light:n.light;n.leds=at?want.leds:n.leds;want=n;
        }else warnedWatchdog=false;
        want.micLed=userTriggersOff;
        want.hd=hdActive&&!isDs4()&&!padBluetooth;   // Bluetooth has no haptics audio channel on Windows
        if(adjustAt&&now-adjustAt<1500){   // Mute + D-pad feedback: white lightbar, LEDs = new level, a short buzz for vibration
            want.light=ds::dim(ds::Rgb{255,255,255},cfg.brightness/100.f);want.leds=ds::gauge_leds(adjustLevel/100.f);
            if(adjustRumble&&now-adjustAt<250)want.rumL=want.rumR=static_cast<uint8_t>(adjustLevel*255/100);}
        if(!dryRun&&(!haveSent||want!=sent||now-lastSent>2000)){if(writeReport(want,false,dev.wov)){sent=want;haveSent=true;lastSent=now;}else if(dev.writeErrors>40)closeDevice("output keeps failing");}
        if(now>=nextSlotCheck&&!warnedSlot){
            nextSlotCheck=now+10000;
            for(const HookedDll& h:dlls){if(!h.original[F_GET])continue;XINPUT_STATE s{};
                if(reinterpret_cast<GetStateFn>(h.original[F_GET])(0,&s)==ERROR_SUCCESS){warnedSlot=true;
                    log("WARNING: the system also reports an XInput pad in slot 0 (Steam Input still on, or an Xbox pad). The DualSense replaces it for the game.");}break;}
        }
        if(now>=nextStatus){nextStatus=now+30000;
            char events[160]="";size_t used=0;
            for(int e=0;e<EV_COUNT;++e)used+=std::snprintf(events+used,sizeof events-used,"%s%s %u",e?", ":"",evName[e],evCount[e]);
            logf("status: input reports %llu, output reports %llu (failed %llu), R2 %s, L2 %s, light %02X%02X%02X, LEDs %02X, %s | %s",
                reportsIn,reportsOut,writeFailures,ds::fx_name(sent.r2),ds::fx_name(sent.l2),sent.light.r,sent.light.g,sent.light.b,sent.leds,hapStatus().c_str(),events);}
    }
    return 0;
}
HANDLE thread=nullptr;
void shutdownPad(){
    // Never leave the triggers stiff: triggers off, motors off, LEDs back to the firmware.
    if(!dryRun)xboxStop();
    if(dev.h==INVALID_HANDLE_VALUE||dryRun)return;
    // The device thread may have died mid-write at process exit: cancel its I/O and use a fresh OVERLAPPED.
    CancelIoEx(dev.h,nullptr);ds::OutState o=neutralOut();o.micLed=false;
    OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ov.hEvent)return;
    writeReport(o,true,ov);CloseHandle(ov.hEvent);
    log("exit: triggers released, rumble off, LEDs handed back");
}
} // namespace

extern "C" BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID reserved){
    if(reason==DLL_PROCESS_ATTACH){
        self=module;startTick=GetTickCount64();mainThread=GetCurrentThreadId();DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&logLock);InitializeCriticalSection(&stateLock);InitializeCriticalSection(&callerLock);InitializeCriticalSection(&hookLock);InitializeCriticalSection(&hapLock);
        GetModuleFileNameW(self,folder,MAX_PATH);wchar_t* slash=wcsrchr(folder,L'\\');if(slash)*slash=0;
        swprintf_s(iniPath,L"%s\\Aleks.DualSense.SA.ini",folder);swprintf_s(logPath,L"%s\\Aleks.DualSense.SA.log",folder);
        wchar_t prev[MAX_PATH];swprintf_s(prev,L"%s\\Aleks.DualSense.SA.prev.log",folder);MoveFileExW(logPath,prev,MOVEFILE_REPLACE_EXISTING);
        logf("Alek's DualSense SA 2.2 (build %s %s), game thread %lu",__DATE__,__TIME__,static_cast<unsigned long>(mainThread));
        loadSettings();buildMenuRows();
        if(!cfg.enabled){log("disabled in the INI ([General] Enabled=0); XInput untouched");return TRUE;}
        gameOk=readable(VERSION_PROBE,sizeof versionBytes)&&std::memcmp(reinterpret_cast<const void*>(VERSION_PROBE),versionBytes,sizeof versionBytes)==0;
        {wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);const wchar_t* base=wcsrchr(exe,L'\\');base=base?base+1:exe;
            dryRun=!gameOk&&_wcsnicmp(base,L"gta",3)!=0;if(dryRun)log("not running inside GTA SA: dry run, the pad is read but no output report is ever sent");}
        log(gameOk?"gta_sa.exe 1.0 US detected: game effects on":"gta_sa.exe is not 1.0 US: only input and game rumble are bridged");
        hookLoadedDlls();
        thread=CreateThread(nullptr,0,deviceThread,nullptr,0,nullptr);
    }else if(reason==DLL_PROCESS_DETACH){
        stopping=true;
        if(reserved==nullptr&&thread){WaitForSingleObject(thread,500);}   // FreeLibrary: let the thread finish; at process exit it is already gone
        shutdownPad();
    }
    return TRUE;
}
