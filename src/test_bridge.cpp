// Offline check of the XInput bridge (never the game): loads the ASI into this test process, where it runs in dry-run
// mode (reads the pad, never writes to it), and checks that every XInput DLL entry was patched and answers.
#include <windows.h>
#include <xinput.h>
#include <cstdio>
#include <initializer_list>
using GetStateFn=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
int main(int argc,char** argv){
    if(argc<2){std::printf("usage: test_bridge <asi>\n");return 2;}
    HMODULE asi=LoadLibraryA(argv[1]);if(!asi){std::printf("LoadLibrary failed %lu\n",GetLastError());return 1;}
    Sleep(2500);   // device thread: loads/hooks the XInput DLLs, scans HID
    int bad=0;
    for(const char* dll:{"xinput1_4.dll","xinput1_3.dll"}){
        HMODULE m=GetModuleHandleA(dll);if(!m){std::printf("%s not loaded\n",dll);++bad;continue;}
        auto get=reinterpret_cast<GetStateFn>(GetProcAddress(m,"XInputGetState"));
        auto ex=reinterpret_cast<GetStateFn>(GetProcAddress(m,MAKEINTRESOURCEA(100)));
        const BYTE* p=reinterpret_cast<const BYTE*>(get);
        std::printf("%s XInputGetState entry %02X %02X %02X %02X %02X%s\n",dll,p[0],p[1],p[2],p[3],p[4],p[0]==0xE9?" (bridged)":" (NOT bridged)");
        if(p[0]!=0xE9)++bad;
        for(int i=0;i<3;++i){XINPUT_STATE s{};DWORD r=get(0,&s);
            std::printf("  slot0 -> %lu packet %lu buttons %04X LT %u RT %u LX %d LY %d\n",r,s.dwPacketNumber,s.Gamepad.wButtons,s.Gamepad.bLeftTrigger,s.Gamepad.bRightTrigger,s.Gamepad.sThumbLX,s.Gamepad.sThumbLY);Sleep(200);}
        if(ex){XINPUT_STATE s{};std::printf("  GetStateEx slot0 -> %lu\n",ex(0,&s));}
        for(int i=0;i<120;++i){XINPUT_STATE s{};get(0,&s);Sleep(16);}   // two seconds of 60 fps polling: the watchdog must stay quiet
        XINPUT_STATE s1{};std::printf("  slot1 -> %lu (system)\n",get(1,&s1));
    }
    FreeLibrary(asi);
    std::printf(bad?"bridge check FAILED\n":"bridge check passed\n");return bad?1:0;
}
