use sha2::{Sha256, Digest};
use std::process::Command;
use std::fs;
use std::path::PathBuf;

const API_URL: &str = "https://api.sltranslate.com";

pub fn get_hwid() -> String {
    let output = match Command::new("wmic")
        .args(["csproduct", "get", "uuid"])
        .output() {
        Ok(o) => o,
        Err(_) => return "unknown".to_string(),
    };
    let text = String::from_utf8_lossy(&output.stdout);
    let uid = text.lines().last().unwrap_or("unknown").trim();
    let mut hasher = Sha256::new();
    hasher.update(uid.as_bytes());
    let hash = hasher.finalize();
    hex::encode(&hash[..16])
}

pub async fn verify(key: &str, hwid: &str) -> Result<serde_json::Value, Box<dyn std::error::Error>> {
    let client = reqwest::Client::new();
    let body = serde_json::json!({"license_key": key, "hwid": hwid});
    let resp = client.post(format!("{}/api/verify", API_URL))
        .json(&body)
        .timeout(std::time::Duration::from_secs(10))
        .send()
        .await?;
    let result: serde_json::Value = resp.json().await?;
    Ok(result)
}

fn app_data_dir() -> PathBuf {
    let mut p = dirs::config_dir().unwrap_or_default();
    p.push("SLTranslator");
    fs::create_dir_all(&p).ok();
    p
}

pub fn save(key: &str) {
    save_with_langs(key, "tr", "en");
}

pub fn save_with_langs(key: &str, source_lang: &str, target_lang: &str) {
    let mut p = app_data_dir();
    p.push("license.json");
    let data = serde_json::json!({"key": key, "source_lang": source_lang, "target_lang": target_lang});
    fs::write(p, data.to_string()).ok();
}

pub fn load() -> String {
    let mut p = app_data_dir();
    p.push("license.json");
    if let Ok(content) = fs::read_to_string(p) {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(&content) {
            return v["key"].as_str().unwrap_or("").to_string();
        }
    }
    String::new()
}
