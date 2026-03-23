#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use tauri::Emitter;
use std::sync::{Arc, Mutex};

mod translator;
mod license;
mod firestorm;
mod keyboard;

pub struct AppState {
    pub license_key: Mutex<String>,
    pub google_api_key: Mutex<String>,
    pub expires: Mutex<i64>,
    pub source_lang: Mutex<String>,
    pub target_lang: Mutex<String>,
    pub hwid: String,
    pub active: Mutex<bool>,
    pub processing: Mutex<bool>,
}

#[tauri::command]
async fn verify_license(key: String, state: tauri::State<'_, Arc<AppState>>) -> Result<serde_json::Value, String> {
    let result = license::verify(&key, &state.hwid).await.map_err(|e| e.to_string())?;

    if result["status"] == "active" {
        *state.license_key.lock().unwrap() = key.clone();
        if let Some(exp) = result["expires"].as_i64() {
            *state.expires.lock().unwrap() = exp;
        }
        if let Some(langs) = result["languages"].as_array() {
            if langs.len() >= 2 {
                *state.source_lang.lock().unwrap() = langs[0].as_str().unwrap_or("tr").to_string();
                *state.target_lang.lock().unwrap() = langs[1].as_str().unwrap_or("en").to_string();
            }
        }
        let src = state.source_lang.lock().unwrap().clone();
        let tgt = state.target_lang.lock().unwrap().clone();
        license::save_with_langs(&key, &src, &tgt);

        // Kill Firestorm if running, then update settings
        if firestorm::is_running() {
            firestorm::kill();
            for _ in 0..10 {
                if !firestorm::is_running() { break; }
                tokio::time::sleep(std::time::Duration::from_secs(1)).await;
            }
        }

        let lk = state.license_key.lock().unwrap().clone();
        let sl = state.source_lang.lock().unwrap().clone();
        firestorm::update_settings(&lk, &sl);

        // Firestorm'u tekrar aç
        firestorm::launch();
    }

    Ok(result)
}

#[tauri::command]
fn minimize_window(window: tauri::Window) {
    let _ = window.minimize();
}

#[tauri::command]
fn set_languages(source: String, target: String, state: tauri::State<'_, Arc<AppState>>) {
    *state.source_lang.lock().unwrap() = source.clone();
    *state.target_lang.lock().unwrap() = target.clone();
    let lk = state.license_key.lock().unwrap().clone();
    license::save_with_langs(&lk, &source, &target);
    firestorm::update_settings(&lk, &source);
}

#[tauri::command]
fn close_window(window: tauri::Window) {
    let _ = window.close();
}

#[tauri::command]
async fn translate_text(text: String, state: tauri::State<'_, Arc<AppState>>) -> Result<String, String> {
    let key = state.license_key.lock().unwrap().clone();
    let target = state.target_lang.lock().unwrap().clone();
    translator::translate(&key, &text, &target).await.map_err(|e| e.to_string())
}

#[tauri::command]
fn toggle_translation(state: tauri::State<'_, Arc<AppState>>) -> bool {
    let mut active = state.active.lock().unwrap();
    *active = !*active;
    if *active {
        keyboard::start_hook();
    } else {
        keyboard::stop_hook();
    }
    *active
}

#[tauri::command]
fn get_status(state: tauri::State<'_, Arc<AppState>>) -> serde_json::Value {
    let expires = *state.expires.lock().unwrap();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as i64;
    let expired = expires > 0 && now > expires;
    if expired && !state.license_key.lock().unwrap().is_empty() {
        *state.active.lock().unwrap() = false;
        *state.license_key.lock().unwrap() = String::new();
        keyboard::stop_hook();
        firestorm::clear_settings();
    }
    serde_json::json!({
        "active": *state.active.lock().unwrap(),
        "expires": expires,
        "expired": expired,
        "source_lang": *state.source_lang.lock().unwrap(),
        "target_lang": *state.target_lang.lock().unwrap(),
    })
}

fn main() {
    let hwid = license::get_hwid();
    let state = Arc::new(AppState {
        license_key: Mutex::new(String::new()),
        google_api_key: Mutex::new(String::new()),
        expires: Mutex::new(0),
        source_lang: Mutex::new("tr".to_string()),
        target_lang: Mutex::new("en".to_string()),
        hwid,
        active: Mutex::new(false),
        processing: Mutex::new(false),
    });

    let state_clone = state.clone();

    tauri::Builder::default()
        .manage(state)
        .invoke_handler(tauri::generate_handler![
            verify_license,
            translate_text,
            toggle_translation,
            get_status,
            minimize_window,
            close_window,
            set_languages,
        ])
        .setup(move |app| {
            // Auto-login with saved license
            let saved = license::load();
            if !saved.is_empty() {
                let s = state_clone.clone();
                let handle = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    if let Ok(result) = license::verify(&saved, &s.hwid).await {
                        if result["status"] == "active" {
                            *s.license_key.lock().unwrap() = saved.clone();
                            if let Some(exp) = result["expires"].as_i64() {
                                *s.expires.lock().unwrap() = exp;
                            }
                            if let Some(langs) = result["languages"].as_array() {
                                if langs.len() >= 2 {
                                    *s.source_lang.lock().unwrap() = langs[0].as_str().unwrap_or("tr").to_string();
                                    *s.target_lang.lock().unwrap() = langs[1].as_str().unwrap_or("en").to_string();
                                }
                            }
                            let sl = s.source_lang.lock().unwrap().clone();
                            firestorm::update_settings(&saved, &sl);
                            let _ = handle.emit("auto-login", serde_json::json!({"ok": true}));
                        }
                    }
                });
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error running app");
}
