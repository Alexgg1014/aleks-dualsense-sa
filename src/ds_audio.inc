// HD haptics output (v2): WASAPI shared-mode stream to the DualSense's own USB audio endpoint ("Speakers (DualSense
// Wireless Controller)", 4 channels). Channels 3 and 4 (back left / back right) drive the actuators; 1 and 2 stay silent,
// so nothing is heard. The game thread queues hits and sets continuous layers; this thread renders them.
// Included by dualsense.cpp inside its namespace.

struct HapEvent{hx::Hit h;float gain,pan,delay;const hx::Clip* clip;};
CRITICAL_SECTION hapLock;HapEvent hapQueue[128];int hapCount=0;hx::Layers hapLayers;unsigned long long hapStamp=0;
std::atomic<bool> hdActive{false};      // stream running and HD enabled: hits are queued, the output report drops rumble emulation
std::atomic<bool> audioRunning{false};
unsigned long long hapFrames=0,hapEvents=0;unsigned hapGlitches=0;
float hdMaster(){return cfg.vibration/100.f*1.3f;}   // HD actuators need a little more headroom than the emulated motors
void hap(const hx::Hit& h,float gain=1.f,float pan=0.f,float delay=0.f){
    if(!hdActive||h.gain<=0)return;
    EnterCriticalSection(&hapLock);if(hapCount<128)hapQueue[hapCount++]={h,gain*hdMaster(),pan,delay,nullptr};LeaveCriticalSection(&hapLock);
}
void hapRecipe(const hx::Recipe& r,float gain=1.f,float delay=0.f){for(int i=0;i<r.n;++i)hap(r.p[i].h,r.p[i].gain*gain,r.p[i].pan,r.p[i].delay+delay);}
uint32_t hapRng=0x2545F491u;
void hapClip(const hx::Clip* c,float gain=1.f,float pan=0.f,float delay=0.f){
    if(!hdActive||!c||c->s.empty())return;
    EnterCriticalSection(&hapLock);if(hapCount<128)hapQueue[hapCount++]={hx::Hit{},gain*hdMaster(),pan,delay,c};LeaveCriticalSection(&hapLock);
}

// ---------- weapon sounds -> haptics (2.2) ----------
// Gunshots felt from the player's own weapon sounds: the EarShot folders of NextGen Weapon Sounds (Immersive Weapons
// Pack) or any EarShot pack with the same folder names. Each shoot*.wav becomes a clip (ds_haptics.h make_clip);
// a shot plays one at random, like EarShot does for the sound. Read once, never written; missing files = synth hits.
struct WeaponSounds{std::vector<hx::Clip> shots;hx::Clip reloadOut,reloadIn;float gain=1.f;};
WeaponSounds weaponSounds[47];std::atomic<bool> weaponSoundsReady{false};
struct SoundFolder{const wchar_t* name;int type;float seconds,gain;};
const SoundFolder soundFolders[]={{L"Pistol",22,0.18f,0.85f},{L"Silenced",23,0.16f,0.70f},{L"Desert Eagle",24,0.25f,1.f},{L"Chromegun",25,0.32f,1.f},
    {L"Sawed Off",26,0.32f,1.f},{L"SPAS 12",27,0.30f,1.f},{L"MicroUzi",28,0.10f,0.65f},{L"MP5",29,0.10f,0.65f},{L"AK47",30,0.14f,0.85f},
    {L"M4",31,0.14f,0.85f},{L"Tec9",32,0.10f,0.65f},{L"Cuntgun",33,0.35f,1.f},{L"Sniper",34,0.35f,1.f}};
bool readWav(const std::wstring& path,float seconds,hx::Clip& out){
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};std::vector<uint8_t> data;DWORD got=0;
    if(GetFileSizeEx(f,&size)&&size.QuadPart>44&&size.QuadPart<32*1024*1024){data.resize(static_cast<size_t>(size.QuadPart));
        if(!ReadFile(f,data.data(),static_cast<DWORD>(data.size()),&got,nullptr))got=0;}
    CloseHandle(f);
    std::vector<float> mono;float rate=0;if(got!=data.size()||!hx::decode_wav(data.data(),data.size(),mono,rate))return false;
    out=hx::make_clip(mono,rate,seconds);return !out.s.empty();
}
void loadWeaponFolder(const SoundFolder& sf,const std::wstring& dir){
    WeaponSounds& w=weaponSounds[sf.type];w.gain=sf.gain;
    for(int i=0;i<10;++i){hx::Clip c;wchar_t n[32];swprintf_s(n,L"\\shoot%d.wav",i);if(readWav(dir+n,sf.seconds,c))w.shots.push_back(std::move(c));}
    readWav(dir+L"\\reload_one.wav",0.15f,w.reloadOut);readWav(dir+L"\\reload_two.wav",0.15f,w.reloadIn);
    char path[400];WideCharToMultiByte(CP_UTF8,0,dir.c_str(),-1,path,sizeof path,nullptr,nullptr);
    logf("haptics: weapon %d from sound files (%zu shots%s): %s",sf.type,w.shots.size(),w.reloadOut.s.empty()?"":", reloads",path);
}
void scanSoundFolders(const std::wstring& dir,int depth,int& budget){
    if(depth>6||budget<=0)return;
    WIN32_FIND_DATAW fd;HANDLE h=FindFirstFileExW((dir+L"\\*").c_str(),FindExInfoBasic,&fd,FindExSearchLimitToDirectories,nullptr,0);
    if(h==INVALID_HANDLE_VALUE)return;
    do{
        if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||fd.cFileName[0]==L'.')continue;
        --budget;std::wstring sub=dir+L"\\"+fd.cFileName;const SoundFolder* match=nullptr;
        for(const SoundFolder& sf:soundFolders)if(!_wcsicmp(fd.cFileName,sf.name))match=&sf;
        if(match&&weaponSounds[match->type].shots.empty()&&GetFileAttributesW((sub+L"\\shoot0.wav").c_str())!=INVALID_FILE_ATTRIBUTES)loadWeaponFolder(*match,sub);
        else if(!match)scanSoundFolders(sub,depth+1,budget);
    }while(budget>0&&FindNextFileW(h,&fd));
    FindClose(h);
}
void loadWeaponSounds(){
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);wchar_t* slash=wcsrchr(exe,L'\\');if(slash)*slash=0;
    int budget=6000;unsigned long long t0=GetTickCount64();
    for(const wchar_t* root:{L"\\modloader",L"\\EarShot",L"\\scripts"})scanSoundFolders(std::wstring(exe)+root,0,budget);
    int weapons=0;for(const WeaponSounds& w:weaponSounds)weapons+=!w.shots.empty();
    if(weapons)logf("haptics: gunshots from the weapon sounds for %d weapons (%llu ms); the rest use the built-in hits",weapons,GetTickCount64()-t0);
    else log("haptics: no EarShot weapon sound folders found (NextGen Weapon Sounds); built-in gunshot hits");
    weaponSoundsReady=true;
}
// The clip for this weapon's shot, or nullptr (then the built-in hit plays).
const hx::Clip* shotClip(unsigned type,float& gain){
    if(!weaponSoundsReady||!cfg.soundShots||type>=47||weaponSounds[type].shots.empty())return nullptr;
    const WeaponSounds& w=weaponSounds[type];gain=w.gain;
    return &w.shots[static_cast<size_t>(hx::rnd(hapRng)*static_cast<float>(w.shots.size()))%w.shots.size()];
}
const hx::Clip* reloadClip(unsigned type,bool insert){
    if(!weaponSoundsReady||!cfg.soundShots||type>=47)return nullptr;
    const hx::Clip& c=insert?weaponSounds[type].reloadIn:weaponSounds[type].reloadOut;return c.s.empty()?nullptr:&c;
}
void hapSetLayers(const hx::Layers& l,unsigned long long now){EnterCriticalSection(&hapLock);hapLayers=l;hapStamp=now;LeaveCriticalSection(&hapLock);}

// Finds the active render endpoint whose friendly name contains "DualSense".
IMMDevice* findPadEndpoint(char* name,size_t size){
    IMMDeviceEnumerator* e=nullptr;IMMDevice* found=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&e))))return nullptr;
    const PROPERTYKEY friendly{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
    IMMDeviceCollection* list=nullptr;UINT count=0;
    if(SUCCEEDED(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&list))&&SUCCEEDED(list->GetCount(&count)))
        for(UINT i=0;i<count&&!found;++i){IMMDevice* d=nullptr;if(FAILED(list->Item(i,&d)))continue;
            IPropertyStore* s=nullptr;bool match=false;
            if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&s))){PROPVARIANT v;PropVariantInit(&v);
                if(SUCCEEDED(s->GetValue(friendly,&v))&&v.vt==VT_LPWSTR&&v.pwszVal&&wcsstr(v.pwszVal,L"DualSense")){match=true;WideCharToMultiByte(CP_UTF8,0,v.pwszVal,-1,name,static_cast<int>(size),nullptr,nullptr);}
                PropVariantClear(&v);s->Release();}
            if(match)found=d;else d->Release();}
    if(list)list->Release();e->Release();return found;
}
// Position of a speaker bit in the interleaved frame (WAVEFORMATEXTENSIBLE channel mask order).
int channelIndex(DWORD mask,DWORD speaker){int idx=0;for(DWORD bit=1;bit;bit<<=1){if(mask&bit){if(bit==speaker)return idx;++idx;}}return -1;}

// One session on the endpoint; returns when the device goes away or the mod stops.
void runHapticStream(IMMDevice* device,const char* name){
    IAudioClient* client=nullptr;IAudioRenderClient* render=nullptr;WAVEFORMATEX* fmt=nullptr;HANDLE ready=nullptr;
    auto cleanup=[&]{if(render)render->Release();if(client){client->Stop();client->Release();}if(fmt)CoTaskMemFree(fmt);if(ready)CloseHandle(ready);hdActive=false;audioRunning=false;};
    if(FAILED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client)))||FAILED(client->GetMixFormat(&fmt))){log("haptics: cannot open the DualSense audio endpoint");cleanup();return;}
    bool isFloat=fmt->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;DWORD mask=0;
    if(fmt->wFormatTag==WAVE_FORMAT_EXTENSIBLE){auto* x=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(fmt);isFloat=x->SubFormat.Data1==3;mask=x->dwChannelMask;}
    int chL=channelIndex(mask,SPEAKER_BACK_LEFT),chR=channelIndex(mask,SPEAKER_BACK_RIGHT);
    if(chL<0||chR<0){chL=2;chR=3;}
    logf("haptics: \"%s\" %u ch %lu Hz %u-bit %s mask %08lX -> actuators on channels %d/%d",name,fmt->nChannels,static_cast<unsigned long>(fmt->nSamplesPerSec),fmt->wBitsPerSample,
        isFloat?"float":"PCM",static_cast<unsigned long>(mask),chL+1,chR+1);
    if(fmt->nChannels<4||chR>=fmt->nChannels||(!isFloat&&fmt->wBitsPerSample!=16&&fmt->wBitsPerSample!=32)){
        log("haptics: the DualSense audio is not 4-channel (Windows sound settings > DualSense speakers > set it to 4 channels / Quadraphonic); using classic rumble");cleanup();return;}
    {IAudioEndpointVolume* vol=nullptr;if(SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&vol)))){
        float level=0;BOOL muted=FALSE;vol->GetMasterVolumeLevelScalar(&level);vol->GetMute(&muted);
        logf("haptics: Windows volume of the DualSense speakers %.0f%%%s (it scales the haptics too)",level*100,muted?", MUTED":"");vol->Release();}}
    ready=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!ready||FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,200000,0,fmt,nullptr))||FAILED(client->SetEventHandle(ready))||
       FAILED(client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(&render)))){log("haptics: stream setup failed; using classic rumble");cleanup();return;}
    UINT32 bufferFrames=0;client->GetBufferSize(&bufferFrames);
    hx::Synth synth(static_cast<float>(fmt->nSamplesPerSec));
    std::vector<float> L(bufferFrames),R(bufferFrames);
    if(FAILED(client->Start())){log("haptics: stream start failed; using classic rumble");cleanup();return;}
    audioRunning=true;logf("haptics: HD stream running (%u-frame buffer, %.0f ms)",bufferFrames,bufferFrames*1000.0/fmt->nSamplesPerSec);
    const int channels=fmt->nChannels,bytes=fmt->wBitsPerSample/8;
    while(!stopping&&padConnected){
        hdActive=cfg.hdHaptics&&!dryRun&&!padBluetooth;
        if(WaitForSingleObject(ready,200)!=WAIT_OBJECT_0)continue;
        UINT32 padding=0;HRESULT hr=client->GetCurrentPadding(&padding);
        if(hr==AUDCLNT_E_DEVICE_INVALIDATED)break;if(FAILED(hr)){++hapGlitches;continue;}
        UINT32 frames=bufferFrames-padding;if(!frames)continue;
        // Pull what the game thread queued. Without a fresh game sample (menus, exit, alt-tab) every layer fades out.
        HapEvent local[128];int n=0;hx::Layers layers;unsigned long long stamp;
        EnterCriticalSection(&hapLock);n=hapCount;std::copy(hapQueue,hapQueue+n,local);hapCount=0;layers=hapLayers;stamp=hapStamp;LeaveCriticalSection(&hapLock);
        unsigned long long now=GetTickCount64();if(!stamp||now>stamp+350)layers=hx::Layers{};
        if(!hdActive){layers=hx::Layers{};n=0;synth.silence();}
        for(int i=0;i<n;++i){if(local[i].clip)synth.trigger(local[i].clip,local[i].gain,local[i].pan,local[i].delay);else synth.trigger(local[i].h,local[i].gain,local[i].pan,local[i].delay);}
        hapEvents+=static_cast<unsigned long long>(n);synth.set(layers);synth.render(L.data(),R.data(),static_cast<int>(frames));
        BYTE* out=nullptr;hr=render->GetBuffer(frames,&out);
        if(hr==AUDCLNT_E_DEVICE_INVALIDATED)break;if(FAILED(hr)||!out){++hapGlitches;continue;}
        std::memset(out,0,static_cast<size_t>(frames)*channels*bytes);
        for(UINT32 f=0;f<frames;++f){
            BYTE* frame=out+static_cast<size_t>(f)*channels*bytes;
            if(isFloat){reinterpret_cast<float*>(frame)[chL]=L[f];reinterpret_cast<float*>(frame)[chR]=R[f];}
            else if(bytes==2){reinterpret_cast<int16_t*>(frame)[chL]=static_cast<int16_t>(L[f]*32767);reinterpret_cast<int16_t*>(frame)[chR]=static_cast<int16_t>(R[f]*32767);}
            else{reinterpret_cast<int32_t*>(frame)[chL]=static_cast<int32_t>(L[f]*2147483000.f);reinterpret_cast<int32_t*>(frame)[chR]=static_cast<int32_t>(R[f]*2147483000.f);}
        }
        render->ReleaseBuffer(frames,0);hapFrames+=frames;
    }
    log(padConnected?"haptics: stream stopped":"haptics: stream stopped (DualSense disconnected)");
    cleanup();
}
DWORD WINAPI audioThread(void*){
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 0;
    bool warned=false;
    if(!dryRun)loadWeaponSounds();   // before the first stream, on this thread: the game thread never waits for disk
    while(!stopping){
        if(!padConnected||!cfg.hdHaptics||isDs4()){Sleep(300);continue;}   // the DualShock 4 has no HD actuators
        char name[160]="";IMMDevice* d=findPadEndpoint(name,sizeof name);
        if(!d){if(!warned){warned=true;log("haptics: no active DualSense audio endpoint (is it disabled in Windows sound settings?); using classic rumble");}Sleep(2000);continue;}
        warned=false;runHapticStream(d,name);d->Release();
        if(!stopping)Sleep(1000);
    }
    CoUninitialize();return 0;
}
std::string hapStatus(){
    char b[160];std::snprintf(b,sizeof b,"haptics %s (%llu hits, %.0f s rendered, %u glitches)",hdActive?"HD":audioRunning?"HD stream idle":"classic rumble",
        hapEvents,hapFrames/48000.0,hapGlitches);
    return b;
}
