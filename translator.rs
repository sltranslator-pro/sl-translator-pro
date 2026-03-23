const API_URL: &str = "https://api.sltranslate.com";

pub async fn translate(license_key: &str, text: &str, target: &str) -> Result<String, Box<dyn std::error::Error>> {
    let client = reqwest::Client::new();
    let body = serde_json::json!({
        "license_key": license_key,
        "text": text,
        "target": target
    });
    let resp = client.post(format!("{}/api/translate", API_URL))
        .json(&body)
        .timeout(std::time::Duration::from_secs(10))
        .send()
        .await?;
    let result: serde_json::Value = resp.json().await?;
    Ok(result["translated"].as_str().unwrap_or("").to_string())
}
