use serde::{Deserialize, Serialize};
use std::collections::VecDeque;
#[derive(Serialize, Deserialize, Clone)]
#[serde(default)]
pub struct Settings {
    #[serde(default)]
    pub palette_version: u32,
    pub host: String,
    pub port: u16,
    pub user: String,
    pub known_hosts: String,
    pub pinned: String,
    pub private_key: String,
    pub echo: u8,
    pub heartbeat: bool,
    pub timing: bool,
    pub doskey: bool,
    pub max_chars: usize,
    pub timeout: u32,
    pub font_size: f32,
    pub dark: bool,
    pub history: Vec<String>,
    pub response_color: [u8; 3],
    pub command_color: [u8; 3],
    pub error_color: [u8; 3],
    pub message_color: [u8; 3],
}
impl Default for Settings {
    fn default() -> Self {
        Self {
            palette_version: 1,
            host: "192.168.0.200".into(),
            port: 22,
            user: "root".into(),
            known_hosts: String::new(),
            pinned: String::new(),
            private_key: String::new(),
            echo: 0,
            heartbeat: false,
            timing: false,
            doskey: true,
            max_chars: 1_000_000,
            timeout: 10000,
            font_size: 14.,
            dark: false,
            history: Vec::new(),
            response_color: [255, 255, 255],
            command_color: original_command_color(),
            error_color: [255, 0, 0],
            message_color: [255, 255, 0],
        }
    }
}
#[derive(Clone, Copy)]
pub enum Kind {
    Response,
    Command,
    Error,
    Message,
}
pub struct Line {
    pub text: String,
    pub kind: Kind,
}
#[derive(Default)]
pub struct Transcript {
    pub lines: VecDeque<Line>,
    chars: usize,
}
impl Transcript {
    pub fn append(&mut self, text: String, kind: Kind, max: usize) {
        for line in text.lines() {
            let mut text = line.to_owned();
            if text.len() > max {
                let mut start = text.len() - max;
                while !text.is_char_boundary(start) {
                    start += 1;
                }
                text = text[start..].to_owned();
            }
            self.chars += text.len();
            self.lines.push_back(Line { text, kind });
        }
        while self.chars > max {
            if let Some(l) = self.lines.pop_front() {
                self.chars -= l.text.len();
            } else {
                break;
            }
        }
    }
    pub fn text(&self) -> String {
        self.lines
            .iter()
            .map(|l| l.text.as_str())
            .collect::<Vec<_>>()
            .join("\n")
    }
    pub fn clear(&mut self) {
        self.lines.clear();
        self.chars = 0;
    }
}
pub fn remember(history: &mut Vec<String>, text: &str) {
    history.retain(|s| s != text);
    history.insert(0, text.into());
    history.truncate(200);
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn history() {
        let mut h = vec!["A".into(), "B".into()];
        remember(&mut h, "B");
        assert_eq!(h, vec!["B", "A"]);
    }
    #[test]
    fn trim_utf8() {
        let mut t = Transcript::default();
        t.append("你好终端".into(), Kind::Response, 7);
        assert_eq!(t.text(), "终端");
    }
    #[test]
    fn no_password_saved() {
        let json = serde_json::to_string(&Settings::default()).unwrap();
        assert!(!json.contains("password"));
    }
}

pub fn original_command_color() -> [u8; 3] {
    #[cfg(windows)] {
        #[link(name = "user32")]
        unsafe extern "system" { fn GetSysColor(index: i32) -> u32; }
        let color = unsafe { GetSysColor(27) };
        [color as u8, (color >> 8) as u8, (color >> 16) as u8]
    }
    #[cfg(not(windows))] { [185, 209, 234] }
}
impl Settings {
    pub fn apply_original_palette(&mut self) {
        self.palette_version = 1;
        self.response_color = [255, 255, 255];
        self.command_color = original_command_color();
        self.error_color = [255, 0, 0];
        self.message_color = [255, 255, 0];
    }
}
#[cfg(test)] mod palette_tests {
    use super::*;
    #[test] fn old_settings_migrate_to_original_palette() {
        let mut settings: Settings = serde_json::from_str(r#"{"host":"device","response_color":[30,40,50]}"#).unwrap();
        assert_eq!(settings.palette_version, 0);
        settings.apply_original_palette();
        assert_eq!(settings.response_color, [255,255,255]);
        assert_eq!(settings.error_color, [255,0,0]);
        assert_eq!(settings.message_color, [255,255,0]);
        assert_eq!(settings.host, "device");
    }
}