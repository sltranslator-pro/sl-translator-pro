// SL Translator Pro - Kontrol HUD'u (tek prim, menü tabanlı)
//
// Kurulum: Bir prim oluştur, bu scripti içine koy, HUD olarak tak
// (Attach to HUD > Top Right önerilir). Prime dokununca menü açılır.
//
// DLL ile iletişim: Komutlar llOwnerSay ile "/tr ..." biçiminde gönderilir.
// DLL bunları CHAT_TYPE_OWNER olarak yakalar (version.cpp, InspectChatFromSimulatorPacket).
// Desteklenen komutlar: /tr on|off, /tr lang <my> <target>, /tr lang auto,
// /tr src on|off, /tr status

// ---------- Görsel panel ----------
// Primün ön yüzüne (FACE) hud_panel_on.png / hud_panel_off.png uygulanır.
// Envantere yüklediğiniz dokuların adı aşağıdakiyle aynı olmalı
// (ya da ad yerine UUID yazın). Buton bölgeleri make_hud_texture.py ile aynıdır.
string TEX_ON  = "hud_panel_on";
string TEX_OFF = "hud_panel_off";
integer FACE = 0; // kutu primde üst yüz; düz panel için kurulum notuna bakın

// id, s0, t0, s1, t1  (doku koordinatı, sol-alt = 0,0)
list BUTTONS = [
    "power",  0.039, 0.355, 0.322, 0.668,
    "swap",   0.358, 0.355, 0.642, 0.668,
    "src",    0.678, 0.355, 0.961, 0.668,
    "my",     0.039, 0.078, 0.256, 0.297,
    "target", 0.275, 0.078, 0.492, 0.297,
    "auto",   0.512, 0.078, 0.729, 0.297,
    "status", 0.748, 0.078, 0.961, 0.297
];

// ---------- Diller (kod, menüde görünen ad) ----------
list LANGS = [
    "tr", "Türkçe",
    "en", "English",
    "de", "Deutsch",
    "fr", "Français",
    "es", "Español",
    "it", "Italiano",
    "pt", "Português",
    "ru", "Русский",
    "nl", "Nederlands",
    "pl", "Polski",
    "ar", "العربية",
    "ja", "日本語",
    "zh", "中文",
    "ko", "한국어",
    "uk", "Українська",
    "sv", "Svenska"
];
integer PAGE_SIZE = 9; // 9 dil + Geri/İleri/Ana menü = 12 buton

// ---------- Durum ----------
integer gOn = TRUE;
integer gSrc = FALSE;      // orijinal metni de ekle
integer gAuto = TRUE;      // TRUE: Firestorm ayarlarını kullan
string  gMy = "tr";
string  gTarget = "en";

// ---------- Menü ----------
integer gChan;
integer gListen;
string  gMenu;             // "main" | "my" | "target"
integer gPage;

// ---------- Yardımcılar ----------
string LangName(string code)
{
    integer i = llListFindList(LANGS, [code]);
    if (i == -1) return llToUpper(code);
    return llList2String(LANGS, i + 1);
}

Send(string cmd)
{
    llOwnerSay(cmd);
}

Save()
{
    llLinksetDataWrite("on", (string)gOn);
    llLinksetDataWrite("src", (string)gSrc);
    llLinksetDataWrite("auto", (string)gAuto);
    llLinksetDataWrite("my", gMy);
    llLinksetDataWrite("target", gTarget);
}

Load()
{
    if (llLinksetDataRead("my") == "") return; // ilk kurulum, varsayılanlar
    gOn = (integer)llLinksetDataRead("on");
    gSrc = (integer)llLinksetDataRead("src");
    gAuto = (integer)llLinksetDataRead("auto");
    gMy = llLinksetDataRead("my");
    gTarget = llLinksetDataRead("target");
}

// Kayıtlı durumu DLL'e yeniden uygula (takıldığında / girişte)
ApplyAll()
{
    if (gOn) Send("/tr on"); else Send("/tr off");
    if (gSrc) Send("/tr src on"); else Send("/tr src off");
    if (gAuto) Send("/tr lang auto");
    else Send("/tr lang " + gMy + " " + gTarget);
}

UpdateText()
{
    string pair;
    if (gAuto) pair = "Firestorm ayarı";
    else pair = llToUpper(gMy) + " → " + llToUpper(gTarget);

    string line1;
    vector color;
    if (gOn) { line1 = "SLT: AÇIK"; color = <0.3, 1.0, 0.4>; }
    else     { line1 = "SLT: KAPALI"; color = <1.0, 0.35, 0.35>; }

    string extra = "";
    if (gSrc) extra = "\n+ orijinal metin";

    llSetText(line1 + "\n" + pair + extra, color, 1.0);
    string tex = TEX_OFF;
    if (gOn) tex = TEX_ON;
    if (llGetInventoryType(tex) == INVENTORY_TEXTURE || (key)tex)
        llSetTexture(tex, FACE);
}

string HitButton(vector st)
{
    integer i;
    integer n = llGetListLength(BUTTONS);
    for (i = 0; i < n; i += 5)
    {
        if (st.x >= llList2Float(BUTTONS, i + 1) && st.x <= llList2Float(BUTTONS, i + 3) &&
            st.y >= llList2Float(BUTTONS, i + 2) && st.y <= llList2Float(BUTTONS, i + 4))
            return llList2String(BUTTONS, i);
    }
    return "";
}

TogglePower()
{
    gOn = !gOn;
    if (gOn) Send("/tr on"); else Send("/tr off");
}

ToggleSrc()
{
    gSrc = !gSrc;
    if (gSrc) Send("/tr src on"); else Send("/tr src off");
    if (gSrc) llOwnerSay("SLT HUD: Orijinal metin eklenecek.");
    else llOwnerSay("SLT HUD: Orijinal metin eklenmeyecek.");
}

SwapPair()
{
    string t = gMy; gMy = gTarget; gTarget = t;
    SetPair();
    llOwnerSay("SLT HUD: " + LangName(gMy) + " → " + LangName(gTarget));
}

AutoPair()
{
    gAuto = TRUE;
    Send("/tr lang auto");
    llOwnerSay("SLT HUD: Dil çifti Firestorm ayarlarına döndü.");
}

OpenListen()
{
    llListenRemove(gListen);
    gListen = llListen(gChan, "", llGetOwner(), "");
    llSetTimerEvent(60.0); // 60 sn sonra dinlemeyi kapat
}

MainMenu()
{
    gMenu = "main";
    string onBtn;
    if (gOn) onBtn = "■ Kapat"; else onBtn = "▶ Aç";
    string srcBtn;
    if (gSrc) srcBtn = "Orijinal ✓"; else srcBtn = "Orijinal ✗";

    string pair;
    if (gAuto) pair = "Firestorm ayarları (otomatik)";
    else pair = LangName(gMy) + " → " + LangName(gTarget);

    string durum;
    if (gOn) durum = "AÇIK"; else durum = "KAPALI";

    // llDialog butonları alttan üste, soldan sağa dizer
    list buttons = [
        "Durum", "Otomatik", "Kapat ✕",
        "Benim dilim", "Hedef dil", "⇄ Değiştir",
        onBtn, srcBtn
    ];
    OpenListen();
    llDialog(llGetOwner(),
        "SL Translator Pro\n\nÇeviri: " + durum +
        "\nDil çifti: " + pair +
        "\n\nBenim dilim = okuduğum dil\nHedef dil = gönderdiğim mesajların dili",
        buttons, gChan);
}

LangMenu(string which)
{
    gMenu = which;
    integer total = llGetListLength(LANGS) / 2;
    integer pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    if (gPage >= pages) gPage = 0;
    if (gPage < 0) gPage = pages - 1;

    list buttons = [];
    integer start = gPage * PAGE_SIZE;
    integer i;
    for (i = start; i < start + PAGE_SIZE && i < total; ++i)
    {
        string code = llList2String(LANGS, i * 2);
        buttons += [llToUpper(code) + " " + llList2String(LANGS, i * 2 + 1)];
    }
    buttons = ["◀ Geri", "Ana menü", "İleri ▶"] + buttons;

    string title;
    if (which == "my") title = "Benim dilim (okuduğum dil)\nŞu an: " + LangName(gMy);
    else title = "Hedef dil (gönderdiğim mesajlar)\nŞu an: " + LangName(gTarget);

    OpenListen();
    llDialog(llGetOwner(), title + "\n\nSayfa " + (string)(gPage + 1) + "/" + (string)pages,
        buttons, gChan);
}

SetPair()
{
    if (gMy == gTarget)
    {
        llOwnerSay("SLT HUD: Benim dilim ve hedef dil aynı olamaz.");
        return;
    }
    gAuto = FALSE;
    Send("/tr lang " + gMy + " " + gTarget);
    Save();
    UpdateText();
}

default
{
    state_entry()
    {
        gChan = -1 - (integer)("0x" + llGetSubString((string)llGetKey(), 0, 6));
        Load();
        UpdateText();
    }

    attach(key id)
    {
        if (id != NULL_KEY)
        {
            // DLL'in hazır olması için kısa bir gecikme
            llSleep(3.0);
            ApplyAll();
            UpdateText();
        }
    }

    on_rez(integer p)
    {
        if (!llGetAttached()) llResetScript();
    }

    changed(integer c)
    {
        if (c & CHANGED_OWNER) { llLinksetDataReset(); llResetScript(); }
    }

    touch_start(integer n)
    {
        if (llDetectedKey(0) != llGetOwner()) return;
        gPage = 0;

        string btn = "";
        if (llDetectedTouchFace(0) == FACE) btn = HitButton(llDetectedTouchST(0));

        if (btn == "power")       TogglePower();
        else if (btn == "swap")   SwapPair();
        else if (btn == "src")    ToggleSrc();
        else if (btn == "auto")   AutoPair();
        else if (btn == "my")     { LangMenu("my"); return; }
        else if (btn == "target") { LangMenu("target"); return; }
        else if (btn == "status")
        {
            Send("/tr status");
            llOwnerSay("SLT HUD: Durum slt_debug.log dosyasına yazıldı.");
            return;
        }
        else { MainMenu(); return; } // butonların dışına dokunuldu: tam menü

        Save();
        UpdateText();
    }

    listen(integer ch, string name, key id, string msg)
    {
        if (gMenu == "main")
        {
            if (msg == "▶ Aç" || msg == "■ Kapat") TogglePower();
            else if (msg == "Orijinal ✗" || msg == "Orijinal ✓") ToggleSrc();
            else if (msg == "Benim dilim") { gPage = 0; LangMenu("my"); return; }
            else if (msg == "Hedef dil")   { gPage = 0; LangMenu("target"); return; }
            else if (msg == "⇄ Değiştir") SwapPair();
            else if (msg == "Otomatik") AutoPair();
            else if (msg == "Durum")
            {
                Send("/tr status");
                llOwnerSay("SLT HUD: Durum DLL ekran bilgisine ve slt_debug.log dosyasına yazıldı.");
            }
            else if (msg == "Kapat ✕")
            {
                llListenRemove(gListen);
                llSetTimerEvent(0.0);
                return;
            }
            Save();
            UpdateText();
            MainMenu();
            return;
        }

        // Dil menüleri
        if (msg == "Ana menü") { MainMenu(); return; }
        if (msg == "◀ Geri")   { --gPage; LangMenu(gMenu); return; }
        if (msg == "İleri ▶")  { ++gPage; LangMenu(gMenu); return; }

        string code = llToLower(llGetSubString(msg, 0, 1));
        if (llListFindList(LANGS, [code]) == -1) { MainMenu(); return; }

        string other = gTarget;
        if (gMenu != "my") other = gMy;
        if (code == other)
        {
            llOwnerSay("SLT HUD: Benim dilim ve hedef dil aynı olamaz.");
            LangMenu(gMenu);
            return;
        }
        if (gMenu == "my") gMy = code;
        else gTarget = code;
        SetPair();
        MainMenu();
    }

    timer()
    {
        llListenRemove(gListen);
        llSetTimerEvent(0.0);
    }
}
