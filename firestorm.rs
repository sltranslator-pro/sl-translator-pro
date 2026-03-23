use std::fs;
use std::env;
use std::path::PathBuf;
use regex::Regex;

static FIRESTORM_PATH: std::sync::Mutex<Option<String>> = std::sync::Mutex::new(None);

pub fn is_running() -> bool {
    match std::process::Command::new("wmic")
        .args(["process", "where", "name like '%Firestorm%'", "get", "ExecutablePath", "/format:list"])
        .output() {
        Ok(output) => {
            let text = String::from_utf8_lossy(&output.stdout);
            for line in text.lines() {
                if line.starts_with("ExecutablePath=") {
                    let path = line.trim_start_matches("ExecutablePath=").trim();
                    if !path.is_empty() {
                        *FIRESTORM_PATH.lock().unwrap() = Some(path.to_string());
                        return true;
                    }
                }
            }
            false
        }
        Err(_) => false,
    }
}

pub fn kill() {
    // Önce yolu kaydet
    is_running();
    let _ = std::process::Command::new("taskkill")
        .args(["/F", "/IM", "Firestorm*"])
        .output();
}

pub fn launch() {
    if let Some(path) = FIRESTORM_PATH.lock().unwrap().as_ref() {
        let _ = std::process::Command::new(path).spawn();
    }
}

pub fn clear_settings() {
    let path = match find_settings() {
        Some(p) => p,
        None => return,
    };
    let mut content = match fs::read_to_string(&path) {
        Ok(c) => c,
        Err(_) => return,
    };
    content = set_or_add(&content, "TranslateChat", "Boolean", "0");
    fs::write(path, content).ok();
}

pub fn update_settings(license_key: &str, source_lang: &str) {
    let path = match find_settings() {
        Some(p) => p,
        None => return,
    };
    if license_key.is_empty() { return; }

    let mut content = match fs::read_to_string(&path) {
        Ok(c) => c,
        Err(_) => return,
    };

    content = set_xml(&content, "TranslateChat", "Boolean", "1");
    content = set_xml(&content, "TranslateLanguage", "String", source_lang);
    content = set_xml(&content, "TranslationService", "String", "azure");

    // Azure key id'sini güncelle (endpoint ve region zaten doğru)
    content = set_azure_id(&content, license_key);

    fs::write(path, content).ok();
}

fn find_settings() -> Option<PathBuf> {
    let appdata = env::var("APPDATA").ok()?;
    let base = PathBuf::from(&appdata);
    for name in &["Firestorm_x64", "Firestorm"] {
        let p = base.join(name).join("user_settings").join("settings.xml");
        if p.exists() { return Some(p); }
    }
    if let Ok(entries) = fs::read_dir(&base) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.starts_with("Firestorm") {
                let p = entry.path().join("user_settings").join("settings.xml");
                if p.exists() { return Some(p); }
            }
        }
    }
    None
}

fn set_or_add(content: &str, key: &str, typ: &str, value: &str) -> String {
    let search = format!("<key>{}</key>", key);
    if content.contains(&search) {
        // Update existing
        set_xml(content, key, typ, value)
    } else {
        // Add before </map>\n</llsd>
        let new_entry = if typ == "Boolean" {
            format!(
                "    <key>{}</key>\n        <map>\n        <key>Comment</key>\n            <string>{}</string>\n        <key>Type</key>\n            <string>{}</string>\n        <key>Value</key>\n            <boolean>{}</boolean>\n        </map>\n",
                key, key, typ, value
            )
        } else {
            format!(
                "    <key>{}</key>\n        <map>\n        <key>Comment</key>\n            <string>{}</string>\n        <key>Type</key>\n            <string>{}</string>\n        <key>Value</key>\n            <string>{}</string>\n        </map>\n",
                key, key, typ, value
            )
        };
        // Handle both \r\n and \n line endings
        if content.contains("    </map>\r\n</llsd>") {
            content.replace("    </map>\r\n</llsd>", &format!("{}    </map>\r\n</llsd>", new_entry))
        } else {
            content.replace("    </map>\n</llsd>", &format!("{}    </map>\n</llsd>", new_entry))
        }
    }
}

fn set_azure_id(content: &str, license_key: &str) -> String {
    // AzureTranslateAPIKey bloğu içindeki <key>id</key> sonrasındaki string'i değiştir
    let azure_start = match content.find("<key>AzureTranslateAPIKey</key>") {
        Some(p) => p,
        None => return content.to_string(),
    };
    let after_azure = &content[azure_start..];
    let id_pos = match after_azure.find("<key>id</key>") {
        Some(p) => azure_start + p,
        None => return content.to_string(),
    };
    let after_id = &content[id_pos..];
    if let Some(s_start) = after_id.find("<string>") {
        let abs_start = id_pos + s_start + 8;
        if let Some(s_end) = content[abs_start..].find("</string>") {
            let mut result = String::with_capacity(content.len());
            result.push_str(&content[..abs_start]);
            result.push_str(license_key);
            result.push_str(&content[abs_start + s_end..]);
            return result;
        }
    }
    content.to_string()
}

fn set_or_add_raw(content: &str, key: &str, value: &str) -> String {
    let search = format!("<key>{}</key>", key);
    let key_tag = search.clone();
    if content.contains(&search) {
        // Find key, then find <key>Value</key>, then replace everything between Value tag and </map>
        let pos = match content.find(&key_tag) {
            Some(p) => p,
            None => return content.to_string(),
        };
        let after = &content[pos..];
        let vpos = match after.find("<key>Value</key>") {
            Some(p) => pos + p,
            None => return content.to_string(),
        };
        let after_value = &content[vpos..];
        // Find the closing </map> after Value
        let value_start = vpos + 16; // after "<key>Value</key>"
        if let Some(map_end) = after_value.find("</map>") {
            let abs_end = vpos + map_end;
            let mut result = String::with_capacity(content.len());
            result.push_str(&content[..value_start]);
            result.push_str("\n            ");
            result.push_str(value);
            result.push_str("\n        ");
            result.push_str(&content[abs_end..]);
            return result;
        }
        content.to_string()
    } else {
        let new_entry = format!(
            "    <key>{}</key>\n        <map>\n        <key>Comment</key>\n            <string>{}</string>\n        <key>Type</key>\n            <string>LLSD</string>\n        <key>Value</key>\n            {}\n        </map>\n",
            key, key, value
        );
        if content.contains("    </map>\r\n</llsd>") {
            content.replace("    </map>\r\n</llsd>", &format!("{}    </map>\r\n</llsd>", new_entry))
        } else {
            content.replace("    </map>\n</llsd>", &format!("{}    </map>\n</llsd>", new_entry))
        }
    }
}

fn set_xml(content: &str, key: &str, typ: &str, value: &str) -> String {
    // Find the key position, then find the Value tag after it
    let key_tag = format!("<key>{}</key>", key);
    let pos = match content.find(&key_tag) {
        Some(p) => p,
        None => return content.to_string(),
    };
    let after = &content[pos..];
    let value_marker = if typ == "Boolean" { "<key>Value</key>" } else { "<key>Value</key>" };
    let vpos = match after.find(value_marker) {
        Some(p) => pos + p,
        None => return content.to_string(),
    };
    let after_value = &content[vpos..];
    if typ == "Boolean" {
        // Find <boolean>X</boolean> after Value
        if let Some(bstart) = after_value.find("<boolean>") {
            let abs_start = vpos + bstart + 9; // after <boolean>
            if let Some(bend) = content[abs_start..].find("</boolean>") {
                let mut result = String::with_capacity(content.len());
                result.push_str(&content[..abs_start]);
                result.push_str(value);
                result.push_str(&content[abs_start + bend..]);
                return result;
            }
        }
    } else {
        // Find <string>X</string> after Value
        if let Some(sstart) = after_value.find("<string>") {
            let abs_start = vpos + sstart + 8; // after <string>
            if let Some(send) = content[abs_start..].find("</string>") {
                let mut result = String::with_capacity(content.len());
                result.push_str(&content[..abs_start]);
                result.push_str(value);
                result.push_str(&content[abs_start + send..]);
                return result;
            }
        }
    }
    content.to_string()
}
