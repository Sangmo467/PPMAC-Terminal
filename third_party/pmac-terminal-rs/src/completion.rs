use serde::{Deserialize, Serialize};
#[derive(Clone, Default, Serialize, Deserialize)]
pub struct Entry {
    pub name: String,
    pub parent: String,
    pub help: String,
    pub alias: String,
    pub gate: String,
}
#[derive(Clone, Default, Serialize, Deserialize)]
pub struct Snapshot {
    pub levels: Vec<Vec<Entry>>,
    pub globals: Vec<Entry>,
    pub programs: Vec<Entry>,
    pub locals: Vec<Entry>,
}
#[derive(Clone)]
pub struct Suggestion {
    pub name: String,
    pub help: String,
}

pub fn normalize(word: &str) -> String {
    let mut out = String::new();
    let mut bracket = false;
    for c in word.chars() {
        match c {
            '[' => {
                bracket = true;
                out.push('[')
            }
            ']' => {
                bracket = false;
                out.push(']')
            }
            _ if !bracket => out.push(c),
            _ => {}
        }
    }
    out
}
pub fn token_start(text: &str) -> usize {
    text.char_indices()
        .rev()
        .find(|(_, c)| c.is_whitespace() || "=+-*/(),;".contains(*c))
        .map(|(i, c)| i + c.len_utf8())
        .unwrap_or(0)
}
impl Snapshot {
    pub fn suggest(&self, text: &str) -> Vec<Suggestion> {
        let start = token_start(text);
        // 以运算符或空白结尾时没有待补全的词元，回退为“列出全部符号”会把
        // `$$$***` 这类原始命令变成一级符号列表（首项为 `#`）。
        if start == text.len() {
            return Vec::new();
        }
        let word = &text[start..];
        let mut parts: Vec<String> = word.split('.').map(normalize).collect();
        let typed = parts.pop().unwrap_or_default().to_lowercase();
        if let Some(first) = parts.first_mut() {
            if let Some(e) = self
                .levels
                .first()
                .and_then(|l| l.iter().find(|e| e.name.eq_ignore_ascii_case(first)))
            {
                if !e.alias.is_empty() && !e.alias.eq_ignore_ascii_case("null") {
                    *first = e.alias.clone();
                }
            }
        }
        let mut entries: Vec<&Entry> = Vec::new();
        if parts.is_empty() {
            if let Some(l) = self.levels.first() {
                entries.extend(l);
            }
            entries.extend(&self.globals);
            entries.extend(&self.programs);
        } else {
            let depth = parts.len();
            let mut valid = true;
            for d in 1..depth {
                if !self.levels.get(d).is_some_and(|l| {
                    l.iter().any(|e| {
                        e.parent.eq_ignore_ascii_case(&parts[d - 1])
                            && normalize(&e.name).eq_ignore_ascii_case(&parts[d])
                    })
                }) {
                    valid = false;
                    break;
                }
            }
            if valid {
                if let Some(level) = self.levels.get(depth) {
                    entries.extend(level.iter().filter(|e| {
                        let parent = &parts[depth - 1];
                        let same = e.parent.eq_ignore_ascii_case(parent);
                        let gate_ok = depth != 2
                            || e.gate.is_empty()
                            || e.gate.eq_ignore_ascii_case("null")
                            || e.gate.eq_ignore_ascii_case(&parts[0]);
                        same && gate_ok
                    }));
                }
            }
            if depth == 1 {
                entries.extend(
                    self.globals
                        .iter()
                        .chain(self.programs.iter())
                        .filter(|e| e.parent.eq_ignore_ascii_case(&parts[0])),
                );
            }
            if depth == 2 {
                let program = format!("{}{}", parts[0], parts[1]);
                entries.extend(self.locals.iter().filter(|e| {
                    e.parent.eq_ignore_ascii_case(&program)
                        || self
                            .programs
                            .iter()
                            .any(|p| p.name.eq_ignore_ascii_case(&parts[1]) && p.alias == e.parent)
                }));
            }
        }
        let mut result: Vec<_> = entries
            .into_iter()
            .filter(|e| e.name.to_lowercase().starts_with(&typed))
            .map(|e| Suggestion {
                name: e.name.clone(),
                help: e.help.clone(),
            })
            .collect();
        result.sort_by_key(|e| e.name.to_lowercase());
        result.dedup_by(|a, b| a.name.eq_ignore_ascii_case(&b.name));
        result
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> Snapshot {
        Snapshot {
            levels: vec![
                vec![Entry {
                    name: "Motor[]".into(),
                    ..Default::default()
                }],
                vec![Entry {
                    name: "Pos".into(),
                    parent: "Motor[]".into(),
                    help: "Position".into(),
                    ..Default::default()
                }],
            ],
            ..Default::default()
        }
    }
    #[test]
    fn member() {
        let s = fixture();
        assert_eq!(s.suggest("P1=Motor[12].P")[0].name, "Pos");
        assert!(s.suggest("Sys.P").is_empty());
    }
    #[test]
    fn unicode() {
        assert_eq!(token_start("变量=Motor[1]."), 7);
        assert_eq!(normalize("Gate1[3]"), "Gate1[]");
    }
    #[test]
    fn no_cross_parent() {
        assert!(fixture().suggest("Nope[1].P").is_empty());
    }
    #[test]
    fn operator_tail_has_no_suggestions() {
        let s = fixture();
        assert!(s.suggest("$$$***").is_empty());
        assert!(s.suggest("1+").is_empty());
        assert!(s.suggest("P1=").is_empty());
    }
}
#[cfg(test)]
mod robustness {
    use super::*;
    #[test]
    fn arbitrary_special_prefixes() {
        let s = Snapshot {
            levels: vec![
                vec![Entry {
                    name: "Motor[]".into(),
                    ..Default::default()
                }],
                vec![Entry {
                    name: "ServoCtrl".into(),
                    parent: "Motor[]".into(),
                    ..Default::default()
                }],
            ],
            ..Default::default()
        };
        for prefix in [
            "",
            "[",
            "]",
            "[[[[",
            "Motor[",
            "Motor[1].",
            "Motor[1]..",
            "中文🙂[.]",
            "\0\r\n\t",
            "\\\"'`$%{}()!@#",
            "Motor[1].伺服🙂",
        ] {
            let _ = s.suggest(prefix);
        }
        let alphabet = [
            '中', '文', '🙂', '[', ']', '.', '\0', '\n', '\r', '\t', '\\', '\'', '"', '`', '&',
            '#', ';', '=', '+', '-', '/', '*', 'M', 'o', 't', 'r', '0', '1',
        ];
        let mut seed = 123456789u64;
        for _ in 0..10000 {
            let mut text = String::new();
            for _ in 0..(seed as usize % 128) {
                seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1);
                text.push(alphabet[(seed >> 32) as usize % alphabet.len()]);
            }
            let _ = s.suggest(&text);
        }
        assert!(s.suggest(&"🙂[".repeat(100000)).is_empty());
    }
}

/// Cursor position in Unicode characters after accepting a completion.
pub fn insertion_cursor(candidate: &str) -> usize {
    candidate.find("[]").map(|i| candidate[..i].chars().count() + 1)
        .unwrap_or_else(|| candidate.chars().count())
}
#[cfg(test)] mod cursor_tests {
    use super::*;
    #[test] fn empty_array_focus() {
        assert_eq!(insertion_cursor("Motor[]"), 6);
        assert_eq!(insertion_cursor("Chan[]"), 5);
        assert_eq!(insertion_cursor("中文🙂[]"), 4);
        assert_eq!(insertion_cursor("A[].B[]"), 2);
        assert_eq!(insertion_cursor("ServoTime"), 9);
        assert_eq!(insertion_cursor("Bad["), 4);
    }
}