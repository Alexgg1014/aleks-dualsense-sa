// In-game settings menu (v1.1), drawn at Present in the Photo Mode's style: GDI-built TrueType atlas, gothic title,
// SA front-end colours and the four-point star. Navigation runs on the game thread from the raw DualSense input;
// while the menu is open the game sees an untouched pad. Included by dualsense.cpp inside its namespace.

// ---------- languages: Spanish source strings, English when SA is not in Spanish (or as chosen in AJUSTES) ----------
int uiLang=0;   // 0 Spanish, 1 English
const char* const translations[][2]={
    {"GATILLOS","TRIGGERS"},{"VIBRACIÓN","VIBRATION"},{"LUCES","LIGHTS"},{"PROBAR","TEST"},{"AJUSTES","SETTINGS"},
    {"Mute + derecha","Mute + right"},{"Doble toque abajo","Double tap down"},{"Solo tecla","Key only"},
    {"Pistola","Pistol"},{"Escopeta","Shotgun"},{"Escopeta de combate","Combat shotgun"},{"Francotirador","Sniper rifle"},{"Lanzacohetes","Rocket launcher"},
    {"Granada","Grenade"},{"Motosierra","Chainsaw"},{"Carro (60 km/h)","Car (60 km/h)"},{"Carro frenando con derrape","Car braking in a skid"},
    {"Moto (60 km/h)","Motorbike (60 km/h)"},{"Bicicleta","Bicycle"},{"Avión / heli","Plane / heli"},
    {"Golpe recibido","Getting hit"},{"Explosión a la izquierda","Explosion on the left"},{"Explosión a la derecha","Explosion on the right"},
    {"Pasos: concreto","Steps: concrete"},{"Pasos: pasto","Steps: grass"},{"Pasos: arena","Steps: sand"},{"Pasos: grava","Steps: gravel"},{"Pasos: madera","Steps: wood"},
    {"Pasos: metal","Steps: metal"},{"Pasos: agua","Steps: water"},{"Choque de frente","Front crash"},{"Choque de lado","Side crash"},{"Aterrizaje","Landing"},
    {"Molotov","Molotov"},{"Cerrojo del rifle","Rifle bolt"},
    {"Gatillos adaptativos","Adaptive triggers"},{"Armas","Weapons"},{"Fuerza en armas","Weapon strength"},{"Disparo en el clic","Fire at the click"},
    {"Manejo","Driving"},{"Fuerza al manejar","Driving strength"},{"Acelerador según velocidad","Throttle follows speed"},
    {"Vibración","Vibration"},{"Al manejar","While driving"},{"Vibración del juego","Game rumble"},{"Disparos","Shots"},{"Golpes recibidos","Hits taken"},
    {"Explosiones","Explosions"},{"Choques","Crashes"},{"Terreno","Terrain"},{"Háptica HD","HD haptics"},{"Pasos (háptica)","Footsteps (haptics)"},
    {"Brillo","Brightness"},{"Color según la vida","Colour by health"},{"LEDs = armadura","LEDs = armour"},{"Velocímetro en vehículo","Speedometer in vehicles"},
    {"Velocímetro lleno a","Speedometer full at"},{"Luces de policía","Police lights"},
    {"Sentir gatillo","Feel a trigger"},{"Probar háptica","Try haptics"},{"Pulso de vibración","Rumble pulse"},{"Restablecer todo","Reset everything"},
    {"Abrir menú con","Open menu with"},{"Mute apaga gatillos","Mute toggles triggers"},{"Eventos en el log","Log events"},{"Idioma","Language"},
    {"Sí","Yes"},{"Pulsa X","Press X"},{"Pulsa X otra vez","Press X again"},{"conectado por USB","connected over USB"},{"sin control","no controller"},
    {"DualShock 4 por USB","DualShock 4 over USB"},{"El DualShock 4 no tiene gatillos adaptativos ni háptica HD.","The DualShock 4 has no adaptive triggers or HD haptics."},
    {"Gatillos: se sienten al momento. Háptica: elige y pulsa X.","Triggers: felt right away. Haptics: pick one and press X."},
    {"Mute + arriba/abajo también cambia la vibración sin abrir el menú.","Mute + up/down also changes vibration without the menu."},
    {"Se guarda en Documentos\\GTA San Andreas User Files.","Saved in Documents\\GTA San Andreas User Files."},
    {"Los cambios se sienten al momento.","Changes apply right away."},
    {"Pestañas","Tabs"},{"Ajustar","Adjust"},{"Probar","Test"},{"Cerrar","Close"},
};
const char* loc(const char* es){
    if(uiLang==0)return es;
    for(const auto& t:translations)if(!std::strcmp(t[0],es))return t[1];
    return es;
}
const char* languageChoices[]={"Auto","Español","English"};
// SA's own language (CMenuManager::m_nPrefsLanguage, FrontEndMenuManager+0x84): 4 = Spanish, anything else = English.
void resolveLanguage(){uiLang=menuLanguage==1?0:menuLanguage==2?1:(mem<BYTE>(0xBA6748+0x84)==4?0:1);}

// ---------- menu model ----------
enum Tab{TAB_TRIGGERS,TAB_VIBRATION,TAB_LIGHTS,TAB_TEST,TAB_OPTIONS,TAB_COUNT};
const char* tabNames[TAB_COUNT]={"GATILLOS","VIBRACIÓN","LUCES","PROBAR","AJUSTES"};
enum Kind{M_TOGGLE,M_PERCENT,M_RANGE,M_CHOICE,M_ACTION};
const char* openChoices[]={"Mute + derecha","Doble toque abajo","Solo tecla"};
const char* testChoices[]={"Pistola","Desert Eagle","Escopeta","Escopeta de combate","Uzi","AK-47","M4","Rifle","Francotirador","Minigun","Lanzacohetes","Granada","Motosierra","Carro (60 km/h)","Carro frenando con derrape","Moto (60 km/h)","Bicicleta","Avión / heli"};
constexpr int TEST_COUNT=static_cast<int>(sizeof testChoices/sizeof testChoices[0]);
int testChoice=0;
enum Action{A_NONE,A_PULSE,A_DEFAULTS,A_HAPTIC};
const char* hapticChoices[]={"Pistola","Escopeta","AK-47","Francotirador","Cerrojo del rifle","Lanzacohetes","Molotov","Golpe recibido","Explosión a la izquierda","Explosión a la derecha",
    "Pasos: concreto","Pasos: pasto","Pasos: arena","Pasos: grava","Pasos: madera","Pasos: metal","Pasos: agua","Choque de frente","Choque de lado","Aterrizaje"};
constexpr int HAPTIC_COUNT=static_cast<int>(sizeof hapticChoices/sizeof hapticChoices[0]);int hapticChoice=0;
void playHapticTest(int i){
    static const unsigned guns[]={22,25,30,34};static const hx::Surface steps[]={hx::S_CONCRETE,hx::S_GRASS,hx::S_SAND,hx::S_GRAVEL,hx::S_WOOD,hx::S_METAL,hx::S_WATER};
    if(i<4)hap(hx::weapon_hit(guns[i]),1.f,0.35f);else if(i==4){hapRecipe(hx::bolt_open());hapRecipe(hx::bolt_close(),1.f,0.45f);}
    else if(i==5)hap(hx::weapon_hit(35),1.f,0.35f);else if(i==6)hapRecipe(hx::glass_break(1.f,0.f,hapRng));else if(i==7)hap(hx::damage_hit(0.8f));
    else if(i==8)hap(hx::explosion_hit(0.9f),1.f,-0.8f);else if(i==9)hap(hx::explosion_hit(0.9f),1.f,0.8f);
    else if(i<17){for(int s=0;s<4;++s)hapRecipe(hx::footstep(steps[i-10],0.8f,(s%2)?0.45f:-0.45f,hapRng),1.f,s*0.38f);}   // four steps
    else if(i==17)hapRecipe(hx::crash(0.9f,hx::Z_FRONT,0.f));else if(i==18)hapRecipe(hx::crash(0.9f,hx::Z_SIDE,-0.9f));else hap(hx::landing_hit(0.8f));
}
struct MenuRow{Tab tab;const char* label;Kind kind;bool* flag;std::atomic<int>* atom;int* value;int lo,hi,step;const char* const* choices;int count;Action action;const wchar_t* section;const wchar_t* key;};
std::vector<MenuRow> menuRows;
void buildMenuRows(){
    auto toggle=[](Tab t,const char* l,bool* f,const wchar_t* s,const wchar_t* k){return MenuRow{t,l,M_TOGGLE,f,nullptr,nullptr,0,1,1,nullptr,0,A_NONE,s,k};};
    auto pct=[](Tab t,const char* l,std::atomic<int>* a,int* v,int lo,int hi,int step,const wchar_t* s,const wchar_t* k){return MenuRow{t,l,M_PERCENT,nullptr,a,v,lo,hi,step,nullptr,0,A_NONE,s,k};};
    Settings& c=cfg;
    menuRows={
        toggle(TAB_TRIGGERS,"Gatillos adaptativos",&c.triggers,L"Triggers",L"AdaptiveTriggers"),
        toggle(TAB_TRIGGERS,"Armas",&c.weaponTriggers,L"Triggers",L"WeaponTriggers"),
        pct(TAB_TRIGGERS,"Fuerza en armas",&c.weaponStrength,nullptr,0,100,5,L"Triggers",L"WeaponStrength"),
        toggle(TAB_TRIGGERS,"Disparo en el clic",&c.fireAtClick,L"Triggers",L"FireAtClick"),
        toggle(TAB_TRIGGERS,"Manejo",&c.drivingTriggers,L"Triggers",L"DrivingTriggers"),
        pct(TAB_TRIGGERS,"Fuerza al manejar",&c.drivingStrength,nullptr,0,100,5,L"Triggers",L"DrivingStrength"),
        toggle(TAB_TRIGGERS,"Acelerador según velocidad",&c.speedResponse,L"Triggers",L"SpeedResponse"),
        pct(TAB_VIBRATION,"Vibración",&c.vibration,nullptr,0,100,5,L"Vibration",L"Vibration"),
        pct(TAB_VIBRATION,"Al manejar",nullptr,&c.drivingVibration,0,100,5,L"Vibration",L"DrivingVibration"),
        toggle(TAB_VIBRATION,"Vibración del juego",&c.gameRumble,L"Vibration",L"GameRumble"),
        toggle(TAB_VIBRATION,"Disparos",&c.shots,L"Vibration",L"Shots"),
        toggle(TAB_VIBRATION,"Golpes recibidos",&c.damage,L"Vibration",L"Damage"),
        toggle(TAB_VIBRATION,"Explosiones",&c.explosions,L"Vibration",L"Explosions"),
        toggle(TAB_VIBRATION,"Choques",&c.crashes,L"Vibration",L"Crashes"),
        toggle(TAB_VIBRATION,"Terreno",&c.terrain,L"Vibration",L"Terrain"),
        toggle(TAB_VIBRATION,"Háptica HD",&c.hdHaptics,L"Haptics",L"HD"),
        toggle(TAB_VIBRATION,"Pasos (háptica)",&c.footsteps,L"Haptics",L"Footsteps"),
        toggle(TAB_LIGHTS,"Lightbar",&c.lightbar,L"Lighting",L"Lightbar"),
        pct(TAB_LIGHTS,"Brillo",nullptr,&c.brightness,0,100,10,L"Lighting",L"Brightness"),
        toggle(TAB_LIGHTS,"Color según la vida",&c.healthColor,L"Lighting",L"HealthColor"),
        toggle(TAB_LIGHTS,"LEDs = armadura",&c.armourLeds,L"Lighting",L"ArmourLeds"),
        toggle(TAB_LIGHTS,"Velocímetro en vehículo",&c.speedometer,L"Lighting",L"Speedometer"),
        MenuRow{TAB_LIGHTS,"Velocímetro lleno a",M_RANGE,nullptr,nullptr,&c.speedKmh,60,300,10,nullptr,0,A_NONE,L"Lighting",L"SpeedometerKmh"},
        toggle(TAB_LIGHTS,"Luces de policía",&c.policeFlashes,L"Lighting",L"PoliceFlashes"),
        MenuRow{TAB_TEST,"Sentir gatillo",M_CHOICE,nullptr,nullptr,&testChoice,0,TEST_COUNT-1,1,testChoices,TEST_COUNT,A_NONE,nullptr,nullptr},
        MenuRow{TAB_TEST,"Probar háptica",M_CHOICE,nullptr,nullptr,&hapticChoice,0,HAPTIC_COUNT-1,1,hapticChoices,HAPTIC_COUNT,A_HAPTIC,nullptr,nullptr},
        MenuRow{TAB_TEST,"Pulso de vibración",M_ACTION,nullptr,nullptr,nullptr,0,0,0,nullptr,0,A_PULSE,nullptr,nullptr},
        MenuRow{TAB_TEST,"Restablecer todo",M_ACTION,nullptr,nullptr,nullptr,0,0,0,nullptr,0,A_DEFAULTS,nullptr,nullptr},
        MenuRow{TAB_OPTIONS,"Abrir menú con",M_CHOICE,nullptr,nullptr,&menuOpenWith,0,2,1,openChoices,3,A_NONE,L"Menu",L"OpenWith"},
        toggle(TAB_OPTIONS,"Mute apaga gatillos",&c.muteToggles,L"General",L"MuteTogglesTriggers"),
        toggle(TAB_OPTIONS,"Touchpad = Select",&c.touchpadAsBack,L"General",L"TouchpadAsBack"),
        toggle(TAB_OPTIONS,"Eventos en el log",&c.logEvents,L"General",L"LogEvents"),
        MenuRow{TAB_OPTIONS,"Idioma",M_CHOICE,nullptr,nullptr,&menuLanguage,0,2,1,languageChoices,3,A_NONE,L"Menu",L"Language"},
    };
}
int rowValue(const MenuRow& r){return r.flag?(*r.flag?1:0):r.atom?r.atom->load():r.value?*r.value:0;}
void setRowValue(const MenuRow& r,int v){v=std::clamp(v,r.lo,r.hi);if(r.flag)*r.flag=v!=0;else if(r.atom)*r.atom=v;else if(r.value)*r.value=v;}
std::string rowText(const MenuRow& r){
    char b[64];int v=rowValue(r);
    switch(r.kind){case M_TOGGLE:return loc(v?"Sí":"No");case M_PERCENT:std::snprintf(b,sizeof b,"%d %%",v);return b;
        case M_RANGE:std::snprintf(b,sizeof b,"%d km/h",v);return b;case M_CHOICE:return loc(r.choices[std::clamp(v,0,r.count-1)]);default:return "";}
}
// Settings the player changes in game live outside the game folder (userIni), so an MO2 sync never overwrites them.
void saveUserSettings(){
    if(!*userIni)return;
    for(const MenuRow& r:menuRows)if(r.section){wchar_t v[16];swprintf_s(v,L"%d",rowValue(r));WritePrivateProfileStringW(r.section,r.key,v,userIni);}
    logf("settings saved to %ls",userIni);
}

// ---------- drawing ----------
struct Vertex{float x,y,z,rhw;DWORD color;};
struct TexVertex{float x,y,z,rhw;DWORD color;float u,v;};
struct Glyph{float u0,v0,u1,v1,w,h,adv;bool ok;};
struct FontAtlas{Glyph g[256]{};IDirect3DTexture9* tex=nullptr;bool tried=false;float px=48,pad=0;};
FontAtlas bodyFont,titleFont;
float uiAlpha=1;
DWORD fade(DWORD c){float a=static_cast<float>(c>>24)*uiAlpha;return (static_cast<DWORD>(std::clamp(a,0.f,255.f))<<24)|(c&0xFFFFFF);}
void rect(IDirect3DDevice9* dev,float x,float y,float w,float h,DWORD c){c=fade(c);Vertex v[]={{x,y,0,1,c},{x+w,y,0,1,c},{x,y+h,0,1,c},{x+w,y,0,1,c},{x+w,y+h,0,1,c},{x,y+h,0,1,c}};dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(Vertex));}
void line(IDirect3DDevice9* dev,float x0,float y0,float x1,float y1,float t,DWORD c){
    float dx=x1-x0,dy=y1-y0,n=std::sqrt(dx*dx+dy*dy);if(n<=0)return;float nx=-dy/n*t/2,ny=dx/n*t/2;c=fade(c);
    Vertex v[]={{x0+nx,y0+ny,0,1,c},{x1+nx,y1+ny,0,1,c},{x0-nx,y0-ny,0,1,c},{x1+nx,y1+ny,0,1,c},{x1-nx,y1-ny,0,1,c},{x0-nx,y0-ny,0,1,c}};dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(Vertex));
}
void buildFont(IDirect3DDevice9* dev,FontAtlas& f,const wchar_t* preferred,const wchar_t* fallback,int weight,float px){
    f.tried=true;f.px=px;const int W=1024,H=1024;
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return;
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof bi.bmiHeader;bi.bmiHeader.biWidth=W;bi.bmiHeader.biHeight=-H;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    void* bits=nullptr;HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bmp||!bits){DeleteDC(dc);return;}
    auto make=[&](const wchar_t* name){return CreateFontW(-static_cast<int>(px),0,0,0,weight,0,0,0,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,name);};
    HFONT font=make(preferred);HGDIOBJ oldFont=SelectObject(dc,font);wchar_t got[64]{};GetTextFaceW(dc,64,got);
    if(_wcsicmp(got,preferred)!=0){SelectObject(dc,oldFont);DeleteObject(font);font=make(fallback);oldFont=SelectObject(dc,font);GetTextFaceW(dc,64,got);}
    HGDIOBJ oldBmp=SelectObject(dc,bmp);std::memset(bits,0,static_cast<size_t>(W)*H*4);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);int x=2,y=2,lineH=tm.tmHeight+4,pad=static_cast<int>(px/8);f.pad=static_cast<float>(pad);
    for(int cp=32;cp<256;++cp){
        if(cp>126&&cp<160)continue;wchar_t ch=static_cast<wchar_t>(cp);SIZE sz{};GetTextExtentPoint32W(dc,&ch,1,&sz);
        int gw=sz.cx+pad*2;if(x+gw>W){x=2;y+=lineH;}if(y+lineH>H)break;TextOutW(dc,x+pad,y,&ch,1);
        f.g[cp]={static_cast<float>(x)/W,static_cast<float>(y)/H,static_cast<float>(x+gw)/W,static_cast<float>(y+tm.tmHeight)/H,static_cast<float>(gw),static_cast<float>(tm.tmHeight),static_cast<float>(sz.cx),true};
        x+=gw;
    }
    GdiFlush();
    if(SUCCEEDED(dev->CreateTexture(W,H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&f.tex,nullptr))&&f.tex){
        D3DLOCKED_RECT r{};
        if(SUCCEEDED(f.tex->LockRect(0,&r,nullptr,0))){
            for(int row=0;row<H;++row){const DWORD* src=static_cast<const DWORD*>(bits)+static_cast<size_t>(row)*W;DWORD* dst=reinterpret_cast<DWORD*>(static_cast<BYTE*>(r.pBits)+row*r.Pitch);
                for(int col=0;col<W;++col)dst[col]=(((src[col]>>8)&0xFF)<<24)|0xFFFFFF;}   // grey coverage -> alpha
            f.tex->UnlockRect(0);
        }
    }
    logf("menu font: %ls (%s)",got,f.tex?"atlas ready":"atlas failed");
    SelectObject(dc,oldBmp);SelectObject(dc,oldFont);DeleteObject(font);DeleteObject(bmp);DeleteDC(dc);
}
// UTF-8 to Latin-1 (accents, ñ, ¿, ¡).
template<class F> void eachCodepoint(const char* s,F f){
    const unsigned char* p=reinterpret_cast<const unsigned char*>(s);
    while(*p){unsigned cp=*p++;if(cp>=0xC0&&cp<0xE0&&(*p&0xC0)==0x80)cp=((cp&0x1F)<<6)|(*p++&0x3F);else if(cp>=0xE0){while((*p&0xC0)==0x80)++p;cp='?';}f(cp<256?cp:'?');}
}
float textW(const char* s,float size,const FontAtlas& f=bodyFont){s=loc(s);float w=0,k=size/f.px;eachCodepoint(s,[&](unsigned cp){if(f.g[cp].ok)w+=f.g[cp].adv*k;});return w;}
enum Align{LEFT,RIGHT,CENTER};
void text(IDirect3DDevice9* dev,float x,float y,const char* s,DWORD color,float size,Align align=LEFT,const FontAtlas& font=bodyFont){
    s=loc(s);
    if(!font.tex)return;
    float k=size/font.px,w=textW(s,size,font);if(align==RIGHT)x-=w;else if(align==CENTER)x-=w/2;
    std::vector<TexVertex> v;v.reserve(std::strlen(s)*6);DWORD c=fade(color);
    eachCodepoint(s,[&](unsigned cp){const Glyph& g=font.g[cp];if(!g.ok)return;
        float x0=x-font.pad*k-0.5f,y0=y-0.5f,x1=x0+g.w*k,y1=y0+g.h*k;
        TexVertex q[6]={{x0,y0,0,1,c,g.u0,g.v0},{x1,y0,0,1,c,g.u1,g.v0},{x0,y1,0,1,c,g.u0,g.v1},{x1,y0,0,1,c,g.u1,g.v0},{x1,y1,0,1,c,g.u1,g.v1},{x0,y1,0,1,c,g.u0,g.v1}};
        v.insert(v.end(),q,q+6);x+=g.adv*k;});
    if(v.empty())return;
    dev->SetTexture(0,font.tex);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);dev->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(v.size()/3),v.data(),sizeof(TexVertex));
    dev->SetTexture(0,nullptr);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
}
// PlayStation face-button symbols in their colours.
enum Sym{S_CROSS,S_CIRCLE,S_SQUARE,S_TRIANGLE};
void symbol(IDirect3DDevice9* dev,float x,float y,float s,Sym k){
    const DWORD c=k==S_CROSS?0xFF7CB6F2:k==S_CIRCLE?0xFFF07C7C:k==S_SQUARE?0xFFE89CD0:0xFF6EDBA0;float t=std::max(1.5f,s*0.11f),m=s*0.2f;
    if(k==S_CROSS){line(dev,x+m,y+m,x+s-m,y+s-m,t,c);line(dev,x+s-m,y+m,x+m,y+s-m,t,c);}
    else if(k==S_CIRCLE){float cx=x+s/2,cy=y+s/2,r=s/2-m;for(int i=0;i<16;++i){float a=i*0.3926991f,b=(i+1)*0.3926991f;line(dev,cx+r*std::cos(a),cy+r*std::sin(a),cx+r*std::cos(b),cy+r*std::sin(b),t,c);}}
    else if(k==S_SQUARE){line(dev,x+m,y+m,x+s-m,y+m,t,c);line(dev,x+s-m,y+m,x+s-m,y+s-m,t,c);line(dev,x+s-m,y+s-m,x+m,y+s-m,t,c);line(dev,x+m,y+s-m,x+m,y+m,t,c);}
    else{line(dev,x+s/2,y+m,x+s-m,y+s-m,t,c);line(dev,x+s-m,y+s-m,x+m,y+s-m,t,c);line(dev,x+m,y+s-m,x+s/2,y+m,t,c);}
}
// The four-point gothic star SA draws next to the selected option.
void star(IDirect3DDevice9* dev,float cx,float cy,float r,DWORD c){
    c=fade(c);float t=r*0.22f;
    Vertex v[]={{cx,cy-r,0,1,c},{cx+t,cy,0,1,c},{cx-t,cy,0,1,c},{cx,cy+r*1.25f,0,1,c},{cx-t,cy,0,1,c},{cx+t,cy,0,1,c},
                {cx-r,cy,0,1,c},{cx,cy-t,0,1,c},{cx,cy+t,0,1,c},{cx+r,cy,0,1,c},{cx,cy+t,0,1,c},{cx,cy-t,0,1,c}};
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,4,v,sizeof(Vertex));
}
void overlayStates(IDirect3DDevice9* dev){
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);dev->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);dev->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);dev->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);dev->SetRenderState(D3DRS_FOGENABLE,FALSE);dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    dev->SetRenderState(D3DRS_LIGHTING,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    dev->SetTexture(0,nullptr);dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    dev->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);dev->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    dev->SetVertexShader(nullptr);dev->SetPixelShader(nullptr);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
}

// ---------- state & input (game thread) ----------
std::atomic<bool> menuOpen{false},menuToggleRequest{false},padHold{false};   // padHold: after closing, the game sees nothing until every button is released
int menuTab=0,menuRow=0;float menuShow=0,selY=-1,tabX=-1;unsigned long long lastUiTick=0;float uiDt=0;
ds::Input menuPrev;unsigned long long repeatAt=0;bool menuDirty=false,confirmDefaults=false;unsigned long long pulseUntil=0;
std::vector<int> rowsOf(int tab){std::vector<int> r;for(int i=0;i<static_cast<int>(menuRows.size());++i)if(menuRows[static_cast<size_t>(i)].tab==tab)r.push_back(i);return r;}
float approach(float cur,float target,float rate){return cur+(target-cur)*(1-std::exp(-rate*uiDt));}
float easeOut(float t){t=std::clamp(t,0.f,1.f);return 1-(1-t)*(1-t)*(1-t);}
void openMenu(bool open){
    if(open==menuOpen)return;
    menuOpen=open;confirmDefaults=false;
    if(open){log("menu opened");selY=tabX=-1;}else{log("menu closed");padHold=true;if(menuDirty){saveUserSettings();menuDirty=false;}}
}
void loadSettings();   // dualsense.cpp: defaults -> mod INI -> user INI
void menuInput(unsigned long long now){
    ds::Input in;EnterCriticalSection(&stateLock);in=lastInput;LeaveCriticalSection(&stateLock);
    auto pressed=[&](bool cur,bool prev){return cur&&!prev;};
    if(menuToggleRequest.exchange(false))openMenu(!menuOpen);
    static bool prevKey=false;bool key=menuKey&&gameFocused()&&(GetAsyncKeyState(menuKey)&0x8000);if(key&&!prevKey)openMenu(!menuOpen);prevKey=key;
    if(!menuOpen){menuPrev=in;return;}
    if(pressed(in.circle,menuPrev.circle)||pressed(in.options,menuPrev.options)){openMenu(false);menuPrev=in;return;}
    if(pressed(in.r1,menuPrev.r1)){menuTab=(menuTab+1)%TAB_COUNT;menuRow=0;confirmDefaults=false;}
    if(pressed(in.l1,menuPrev.l1)){menuTab=(menuTab+TAB_COUNT-1)%TAB_COUNT;menuRow=0;confirmDefaults=false;}
    // D-pad with auto-repeat: first step on press, then every 90 ms after 350 ms.
    int dir=in.mute?8:in.hat,prevDir=menuPrev.mute?8:menuPrev.hat;bool step=false;
    if(dir!=8&&dir!=prevDir){step=true;repeatAt=now+350;}else if(dir!=8&&now>=repeatAt){step=true;repeatAt=now+90;}
    auto rows=rowsOf(menuTab);int count=static_cast<int>(rows.size());
    if(step&&count){
        if(dir==0){menuRow=(menuRow+count-1)%count;confirmDefaults=false;}
        else if(dir==4){menuRow=(menuRow+1)%count;confirmDefaults=false;}
        else if(dir==2||dir==6){
            const MenuRow& r=menuRows[static_cast<size_t>(rows[static_cast<size_t>(menuRow)])];
            if(r.kind!=M_ACTION){int v=rowValue(r),d=dir==2?1:-1;
                if(r.kind==M_TOGGLE)v=!v;else if(r.kind==M_CHOICE)v=(v+d+r.count)%r.count;else v+=d*r.step;
                setRowValue(r,v);if(r.section)menuDirty=true;
                if(r.kind==M_PERCENT&&r.atom==&cfg.vibration)pulseUntil=now+250;   // feel the new level
            }
        }
    }
    if(count&&pressed(in.cross,menuPrev.cross)){
        const MenuRow& r=menuRows[static_cast<size_t>(rows[static_cast<size_t>(menuRow)])];
        if(r.action==A_PULSE){pulseUntil=now+400;hap(hx::Hit{150,60,0.3f,0.1f,0.3f,1.f});}
        else if(r.action==A_HAPTIC){if(hdActive)playHapticTest(hapticChoice);else log("menu: haptic test needs HD haptics (see the log for why it is off)");}
        else if(r.action==A_DEFAULTS){
            if(!confirmDefaults)confirmDefaults=true;
            else{confirmDefaults=false;if(*userIni)DeleteFileW(userIni);loadSettings();menuDirty=false;log("menu: settings restored to the mod INI");}
        }
    }
    menuPrev=in;
}
// While the menu shows PROBAR > Sentir gatillo, both triggers carry that effect so it can be felt without the game.
bool menuTriggerPreview(ds::TriggerPair& out){
    if(!menuOpen||menuTab!=TAB_TEST)return false;
    static const unsigned weapons[]={22,24,25,27,28,30,31,33,34,38,35,16,9};
    if(testChoice<13){ds::WeaponState w;w.type=weapons[testChoice];out=ds::weapon_triggers(w,cfg.weaponStrength,false);if(w.type==9)out.r2=ds::fx_vibration(1,ds::scale(6,cfg.weaponStrength),38);}
    else{ds::VehicleState v;v.kmh=60;v.kind=testChoice==15?ds::V_BIKE:testChoice==16?ds::V_BMX:testChoice==17?ds::V_AIR:ds::V_CAR;
        if(testChoice==14){v.braking=v.skidding=true;}out=ds::vehicle_triggers(v,cfg.drivingStrength,cfg.speedResponse,static_cast<float>(cfg.speedKmh));}
    if(out.l2==ds::fx_off())out.l2=out.r2;
    return true;
}

// ---------- menu drawing ----------
constexpr DWORD SA_TEXT=0xFFE8EEF6,SA_DIM=0xFF8794A6,ACCENT=0xFFA8C0E6;
DWORD withAlpha(DWORD c,BYTE a){return (static_cast<DWORD>(a)<<24)|(c&0xFFFFFF);}
void drawMenu(IDirect3DDevice9* dev,float w,float h){
    resolveLanguage();
    float k=h/1080.f;(void)w;
    if(!bodyFont.tried)buildFont(dev,bodyFont,L"Bahnschrift SemiBold",L"Segoe UI Semibold",FW_SEMIBOLD,48);
    if(!titleFont.tried)buildFont(dev,titleFont,L"Old English Text MT",L"Bahnschrift SemiBold",FW_NORMAL,64);
    uiAlpha=easeOut(menuShow);float slide=(1-uiAlpha)*-60*k;
    auto rows=rowsOf(menuTab);int count=static_cast<int>(rows.size());if(menuRow>=count)menuRow=0;
    float x0=48*k+slide,y0=56*k,pw=620*k,headerH=74*k,tabH=44*k,rowH=36*k,footH=46*k,pad=24*k;
    float bodyH=rowH*static_cast<float>(std::max(count,7))+44*k,ph=headerH+tabH+bodyH+footH;
    rect(dev,x0,y0,pw,ph,0xC8000000);
    text(dev,x0+pad,y0+8*k,"DualSense",ACCENT,52*k,LEFT,titleFont);
    text(dev,x0+pw-pad,y0+30*k,!padConnected?"sin control":isDs4()?"DualShock 4 por USB":"conectado por USB",padConnected?SA_DIM:0xFFF07C7C,17*k,RIGHT);
    rect(dev,x0+pad,y0+headerH-2*k,pw-pad*2,2*k,withAlpha(ACCENT,0x70));
    float ty=y0+headerH,cw=(pw-pad*2)/TAB_COUNT,target=x0+pad+cw*menuTab;if(tabX<0)tabX=target;tabX=approach(tabX,target,22);
    for(int t=0;t<TAB_COUNT;++t)text(dev,x0+pad+cw*t+cw/2,ty+12*k,tabNames[t],t==menuTab?SA_TEXT:SA_DIM,18*k,CENTER);
    rect(dev,tabX+cw*0.15f,ty+tabH-8*k,cw*0.7f,3*k,ACCENT);
    float by=ty+tabH+4*k,targetY=by+rowH*static_cast<float>(menuRow);if(selY<0)selY=targetY;selY=approach(selY,targetY,24);
    if(count){rect(dev,x0+8*k,selY,pw-16*k,rowH-2*k,withAlpha(ACCENT,0x26));star(dev,x0+pad-4*k,selY+rowH*0.46f,9*k,ACCENT);}
    for(int i=0;i<count;++i){
        const MenuRow& r=menuRows[static_cast<size_t>(rows[static_cast<size_t>(i)])];bool sel=i==menuRow;float y=by+rowH*i+7*k;
        text(dev,x0+pad+14*k,y,r.label,sel?SA_TEXT:SA_DIM,20*k);
        if(r.kind==M_ACTION){
            if(sel){const char* hint=r.action==A_DEFAULTS?(confirmDefaults?"Pulsa X otra vez":"Pulsa X"):"Pulsa X";
                text(dev,x0+pw-pad-28*k,y,hint,ACCENT,18*k,RIGHT);symbol(dev,x0+pw-pad-24*k,y+1*k,22*k,S_CROSS);}
            continue;}
        std::string v=rowText(r);if(sel)v="<  "+v+"  >";
        text(dev,x0+pw-pad,y,v.c_str(),sel?ACCENT:SA_DIM,20*k,RIGHT);
    }
    // Footer note per tab
    const char* note=isDs4()&&(menuTab==TAB_TRIGGERS||menuTab==TAB_VIBRATION)?"El DualShock 4 no tiene gatillos adaptativos ni háptica HD.":
        menuTab==TAB_TEST?"Gatillos: se sienten al momento. Háptica: elige y pulsa X.":
        menuTab==TAB_VIBRATION?"Mute + arriba/abajo también cambia la vibración sin abrir el menú.":
        menuTab==TAB_OPTIONS?"Se guarda en Documentos\\GTA San Andreas User Files.":"Los cambios se sienten al momento.";
    text(dev,x0+pad,by+rowH*static_cast<float>(std::max(count,7))+10*k,note,SA_DIM,16*k);
    float fy=y0+ph-footH,fx=x0+pad,is=22*k;rect(dev,x0+pad,fy,pw-pad*2,1*k,withAlpha(ACCENT,0x50));
    auto label=[&](const char* t){text(dev,fx,fy+13*k,t,SA_DIM,17*k);fx+=textW(t,17*k)+18*k;};
    auto cap=[&](const char* t){float bw=textW(t,14*k)+14*k;rect(dev,fx,fy+12*k,bw,is,0x50FFFFFF);rect(dev,fx+1,fy+12*k+1,bw-2,is-2,0xD0101010);
        text(dev,fx+bw/2,fy+15*k,t,SA_TEXT,14*k,CENTER);fx+=bw+4*k;};
    cap("L1");cap("R1");label("Pestañas");cap("<  >");label("Ajustar");
    symbol(dev,fx,fy+12*k,is,S_CROSS);fx+=is+4*k;label("Probar");symbol(dev,fx,fy+12*k,is,S_CIRCLE);fx+=is+4*k;label("Cerrar");
    uiAlpha=1;
}
// Present hook: the game thread, after the frame is complete.
using PresentFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
PresentFn oldPresent=nullptr;IDirect3DDevice9* hookedDevice=nullptr;bool presentLogged=false,presentHookTried=false;
HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9* dev,const RECT* s,const RECT* d,HWND hw,const RGNDATA* r){
    if(dev==hookedDevice){
        if(!presentLogged){presentLogged=true;log("menu: first Present");}
        unsigned long long now=GetTickCount64();uiDt=lastUiTick?std::min(0.1f,(now-lastUiTick)/1000.f):0.f;lastUiTick=now;
        menuInput(now);
        menuShow=approach(menuShow,menuOpen?1.f:0.f,14);if(!menuOpen&&menuShow<0.01f)menuShow=0;
        if(menuShow>0){
            IDirect3DSurface9* back=nullptr;
            if(SUCCEEDED(dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))&&back){
                D3DSURFACE_DESC desc{};back->GetDesc(&desc);
                IDirect3DStateBlock9* saved=nullptr;
                if(SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL,&saved))&&saved){
                    IDirect3DSurface9* oldTarget=nullptr;dev->GetRenderTarget(0,&oldTarget);dev->SetRenderTarget(0,back);
                    D3DVIEWPORT9 vp{0,0,desc.Width,desc.Height,0,1};dev->SetViewport(&vp);
                    if(SUCCEEDED(dev->BeginScene())){overlayStates(dev);drawMenu(dev,static_cast<float>(desc.Width),static_cast<float>(desc.Height));dev->EndScene();}
                    dev->SetRenderTarget(0,oldTarget);if(oldTarget)oldTarget->Release();
                    saved->Apply();saved->Release();
                }
                back->Release();
            }
        }
    }
    return oldPresent(dev,s,d,hw,r);
}
void hookPresent(){
    auto* dev=mem<IDirect3DDevice9*>(0xC97C28);   // RwD3D9Device (the Photo Mode hooks the same table the same way)
    if(!plausible(reinterpret_cast<uintptr_t>(dev)))return;
    presentHookTried=true;
    void** table=*reinterpret_cast<void***>(dev);DWORD old;
    if(!VirtualProtect(&table[17],sizeof(void*),PAGE_READWRITE,&old))return;
    oldPresent=reinterpret_cast<PresentFn>(table[17]);hookedDevice=dev;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&table[17]),static_cast<LONG>(reinterpret_cast<uintptr_t>(&present)));
    VirtualProtect(&table[17],sizeof(void*),old,&old);
    char w[128];logf("menu: Present hooked (previous %s)",where(reinterpret_cast<uintptr_t>(oldPresent),w,sizeof w));
}
