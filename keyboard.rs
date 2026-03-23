#[cfg(target_os = "windows")]
mod windows_hook {
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicBool, AtomicIsize, Ordering};
    use std::sync::{Mutex, OnceLock};
    use std::thread::{self, JoinHandle};
    use std::time::Duration;

    use windows::core::PCWSTR;
    use windows::Win32::Foundation::{HINSTANCE, LPARAM, LRESULT, WPARAM};
    use windows::Win32::System::LibraryLoader::GetModuleHandleW;
    use windows::Win32::UI::Input::KeyboardAndMouse::{
        SendInput, INPUT, INPUT_0, INPUT_KEYBOARD, KEYBDINPUT, KEYEVENTF_KEYUP, VIRTUAL_KEY, VK_CONTROL, VK_RETURN,
    };
    use windows::Win32::UI::WindowsAndMessaging::{
        CallNextHookEx, DispatchMessageW, GetForegroundWindow, GetWindowTextLengthW, GetWindowTextW, PeekMessageW, SetWindowsHookExW,
        TranslateMessage, UnhookWindowsHookEx, HHOOK, KBDLLHOOKSTRUCT, KBDLLHOOKSTRUCT_FLAGS, MSG, PM_REMOVE, WINDOWS_HOOK_ID, WM_KEYDOWN,
    };

    const WH_KEYBOARD_LL_ID: WINDOWS_HOOK_ID = WINDOWS_HOOK_ID(13);
    const LLKHF_INJECTED_FLAG: KBDLLHOOKSTRUCT_FLAGS = KBDLLHOOKSTRUCT_FLAGS(0x0000_0010);

    static HOOK_ACTIVE: AtomicBool = AtomicBool::new(false);
    static PROCESSING: AtomicBool = AtomicBool::new(false);
    static HOOK_HANDLE: AtomicIsize = AtomicIsize::new(0);

    struct ThreadState {
        join: Option<JoinHandle<()>>,
    }

    static THREAD_STATE: OnceLock<Mutex<ThreadState>> = OnceLock::new();

    fn thread_state() -> &'static Mutex<ThreadState> {
        THREAD_STATE.get_or_init(|| Mutex::new(ThreadState { join: None }))
    }

    fn current_hook_handle() -> Option<HHOOK> {
        let raw = HOOK_HANDLE.load(Ordering::SeqCst);
        if raw == 0 { None } else { Some(HHOOK(raw as _)) }
    }

    fn set_hook_handle(handle: Option<HHOOK>) {
        let raw = handle.map(|h| h.0 as isize).unwrap_or(0);
        HOOK_HANDLE.store(raw, Ordering::SeqCst);
    }

    pub fn start_hook() {
        let old_join = {
            let mut state = thread_state().lock().unwrap();
            if let Some(existing) = state.join.as_ref() {
                if !existing.is_finished() {
                    HOOK_ACTIVE.store(true, Ordering::SeqCst);
                    return;
                }
            }
            state.join.take()
        };

        if let Some(old_join) = old_join {
            let _ = old_join.join();
        }

        HOOK_ACTIVE.store(true, Ordering::SeqCst);
        let mut state = thread_state().lock().unwrap();
        state.join = Some(thread::spawn(|| unsafe { hook_thread_main() }));
    }

    pub fn stop_hook() {
        let join = {
            let mut state = thread_state().lock().unwrap();
            HOOK_ACTIVE.store(false, Ordering::SeqCst);
            state.join.take()
        };

        if let Some(join) = join {
            let _ = join.join();
        }
    }

    #[allow(dead_code)]
    pub fn is_active() -> bool {
        HOOK_ACTIVE.load(Ordering::SeqCst)
    }

    unsafe fn hook_thread_main() {
        let module: HINSTANCE = match GetModuleHandleW(PCWSTR::null()) {
            Ok(hmodule) => HINSTANCE::from(hmodule),
            Err(_) => {
                HOOK_ACTIVE.store(false, Ordering::SeqCst);
                return;
            }
        };

        let hook = match SetWindowsHookExW(WH_KEYBOARD_LL_ID, Some(hook_proc), Some(module), 0) {
            Ok(h) => h,
            Err(_) => {
                HOOK_ACTIVE.store(false, Ordering::SeqCst);
                return;
            }
        };

        set_hook_handle(Some(hook));

        let mut msg = MSG::default();
        while HOOK_ACTIVE.load(Ordering::SeqCst) {
            while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            thread::sleep(Duration::from_millis(20));
        }

        let _ = UnhookWindowsHookEx(hook);
        set_hook_handle(None);
        PROCESSING.store(false, Ordering::SeqCst);

        // Drain remaining messages to avoid leaking WM_QUIT, etc.
        while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {}
    }

    unsafe extern "system" fn hook_proc(ncode: i32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
        if ncode >= 0 && wparam.0 == WM_KEYDOWN as usize && HOOK_ACTIVE.load(Ordering::SeqCst) {
            let kb = *(lparam.0 as *const KBDLLHOOKSTRUCT);

            let injected = kb.flags.contains(LLKHF_INJECTED_FLAG);
            let enter = kb.vkCode == VK_RETURN.0 as u32;

            if enter && !injected && !PROCESSING.load(Ordering::SeqCst) && is_firestorm_foreground() {
                PROCESSING.store(true, Ordering::SeqCst);
                thread::spawn(|| {
                    do_translate_and_send();
                    PROCESSING.store(false, Ordering::SeqCst);
                });
                return LRESULT(1);
            }
        }

        CallNextHookEx(current_hook_handle(), ncode, wparam, lparam)
    }

    fn is_firestorm_foreground() -> bool {
        unsafe {
            let hwnd = GetForegroundWindow();
            let len = GetWindowTextLengthW(hwnd);
            if len <= 0 {
                return false;
            }

            let mut buf = vec![0u16; (len as usize) + 1];
            let read = GetWindowTextW(hwnd, &mut buf) as usize;
            let title = String::from_utf16_lossy(&buf[..read]).to_lowercase();
            title.contains("firestorm")
        }
    }

    fn do_translate_and_send() {
        if !HOOK_ACTIVE.load(Ordering::SeqCst) {
            return;
        }

        thread::sleep(Duration::from_millis(30));
        let old_clipboard = get_clipboard_string();

        send_ctrl_combo(VIRTUAL_KEY(0x41)); // A
        thread::sleep(Duration::from_millis(30));
        send_ctrl_combo(VIRTUAL_KEY(0x43)); // C
        thread::sleep(Duration::from_millis(80));

        let copied = get_clipboard_string();
        let copied = copied.trim().to_string();

        if !copied.is_empty() && copied != old_clipboard {
            if let Some(translated) = translate_via_api(&copied) {
                let translated = translated.trim().to_string();
                if !translated.is_empty() && !eq_case_insensitive(&translated, &copied) {
                    set_clipboard_string(&translated);
                    send_ctrl_combo(VIRTUAL_KEY(0x41)); // A
                    thread::sleep(Duration::from_millis(30));
                    send_ctrl_combo(VIRTUAL_KEY(0x56)); // V
                    thread::sleep(Duration::from_millis(50));
                }
            }
        }

        send_key(VK_RETURN);
        thread::sleep(Duration::from_millis(80));

        if !old_clipboard.is_empty() {
            set_clipboard_string(&old_clipboard);
        }
    }

    fn eq_case_insensitive(a: &str, b: &str) -> bool {
        a.to_lowercase() == b.to_lowercase()
    }

    fn license_file_path() -> Option<PathBuf> {
        let base = std::env::var_os("APPDATA")
            .or_else(|| dirs::config_dir().map(|p| p.into_os_string()))?;
        let mut path = PathBuf::from(base);
        path.push("SLTranslator");
        path.push("license.json");
        Some(path)
    }

    fn read_license_key() -> Option<String> {
        let path = license_file_path()?;
        let data = std::fs::read_to_string(path).ok()?;
        let v: serde_json::Value = serde_json::from_str(&data).ok()?;

        v.get("key")
            .and_then(|k| k.as_str())
            .or_else(|| v.get("license_key").and_then(|k| k.as_str()))
            .or_else(|| v.get("licenseKey").and_then(|k| k.as_str()))
            .map(|s| s.to_string())
    }

    fn read_languages() -> (String, String) {
        // AppData'dan dil ayarını oku
        if let Some(path) = license_file_path() {
            if let Ok(data) = std::fs::read_to_string(&path) {
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(&data) {
                    let from = v.get("source_lang").and_then(|s| s.as_str()).unwrap_or("tr").to_string();
                    let to = v.get("target_lang").and_then(|s| s.as_str()).unwrap_or("en").to_string();
                    return (from, to);
                }
            }
        }
        ("tr".to_string(), "en".to_string())
    }

    fn translate_via_api(text: &str) -> Option<String> {
        let license_key = read_license_key()?;

        let client = reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(10))
            .build()
            .ok()?;

        // source ve target dili license.json'dan veya appdata'dan oku
        let (from_lang, to_lang) = read_languages();
        let body = serde_json::json!({
            "license_key": license_key,
            "text": text,
            "source": from_lang,
            "target": to_lang,
        });

        let resp = client
            .post("https://api.sltranslate.com/api/translate")
            .json(&body)
            .send()
            .ok()?;

        let result: serde_json::Value = match resp.json() {
            Ok(v) => v,
            Err(_) => {
                HOOK_ACTIVE.store(false, Ordering::SeqCst);
                crate::firestorm::clear_settings();
                thread::sleep(Duration::from_secs(10));
                std::process::exit(0);
            }
        };
        if result.get("error").is_some() {
            HOOK_ACTIVE.store(false, Ordering::SeqCst);
            crate::firestorm::clear_settings();
            thread::sleep(Duration::from_secs(10));
            std::process::exit(0);
        }
        result.get("translated").and_then(|v| v.as_str()).map(|s| s.to_string())
    }

    fn get_clipboard_string() -> String {
        clipboard_win::get_clipboard_string().unwrap_or_default()
    }

    fn set_clipboard_string(text: &str) {
        let _ = clipboard_win::set_clipboard_string(text);
    }

    fn input_key(vk: VIRTUAL_KEY, key_up: bool) -> INPUT {
        INPUT {
            r#type: INPUT_KEYBOARD,
            Anonymous: INPUT_0 {
                ki: KEYBDINPUT {
                    wVk: vk,
                    dwFlags: if key_up { KEYEVENTF_KEYUP } else { Default::default() },
                    ..Default::default()
                },
            },
        }
    }

    fn send_ctrl_combo(vk: VIRTUAL_KEY) {
        unsafe {
            let inputs = [
                input_key(VK_CONTROL, false),
                input_key(vk, false),
                input_key(vk, true),
                input_key(VK_CONTROL, true),
            ];
            let _ = SendInput(&inputs, std::mem::size_of::<INPUT>() as i32);
        }
    }

    fn send_key(vk: VIRTUAL_KEY) {
        unsafe {
            let inputs = [input_key(vk, false), input_key(vk, true)];
            let _ = SendInput(&inputs, std::mem::size_of::<INPUT>() as i32);
        }
    }
}

#[cfg(target_os = "windows")]
pub fn start_hook() {
    windows_hook::start_hook();
}

#[cfg(target_os = "windows")]
pub fn stop_hook() {
    windows_hook::stop_hook();
}

#[cfg(target_os = "windows")]
#[allow(dead_code)]
pub fn is_active() -> bool {
    windows_hook::is_active()
}

#[cfg(not(target_os = "windows"))]
pub fn start_hook() {}

#[cfg(not(target_os = "windows"))]
pub fn stop_hook() {}

#[cfg(not(target_os = "windows"))]
#[allow(dead_code)]
pub fn is_active() -> bool {
    false
}
