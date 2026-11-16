//! Native controller symbol tables. Field positions verified against 192.168.0.200.
use crate::{
    completion::{Entry, Snapshot},
    sdk::Sdk,
};
pub const FILES: [&str; 7] = [
    "pp_swtbl0.sym",
    "pp_swtbl1.sym",
    "pp_swtbl2.sym",
    "pp_swtbl3.sym",
    "pp_global.sym",
    "pp_prog.sym",
    "pp_local.sym",
];
// Match MySQL LOAD DATA default backslash decoding, including escaped physical newlines.
pub fn rows(input: &str, delimiter: char) -> Result<Vec<Vec<String>>, String> {
    let mut rows = Vec::new();
    let mut row = Vec::new();
    let mut field = String::new();
    let mut chars = input.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            '\\' => match chars.next() {
                Some('N') if field.is_empty() => {}
                Some('n') => field.push('\n'),
                Some('r') => field.push('\r'),
                Some('t') => field.push('\t'),
                Some('0') => field.push('\0'),
                Some('b') => field.push('\u{8}'),
                Some('Z') => field.push('\u{1a}'),
                Some('\r') => {
                    if chars.peek() == Some(&'\n') {
                        chars.next();
                    }
                    field.push('\n');
                }
                Some(c) => field.push(c),
                None => return Err("符号文件末尾存在不完整转义".into()),
            },
            '\r' if chars.peek() == Some(&'\n') => {}
            '\n' => {
                row.push(std::mem::take(&mut field));
                if row.iter().any(|v| !v.is_empty()) {
                    rows.push(std::mem::take(&mut row));
                } else {
                    row.clear();
                }
            }
            c if c == delimiter => row.push(std::mem::take(&mut field)),
            c => field.push(c),
        }
    }
    if !field.is_empty() || !row.is_empty() {
        row.push(field);
        rows.push(row);
    }
    Ok(rows)
}
fn clean(s: &str) -> String {
    if s.eq_ignore_ascii_case("null") {
        String::new()
    } else {
        s.trim().into()
    }
}
pub fn parse(files: &[String]) -> Result<Snapshot, String> {
    if files.len() != 7 {
        return Err("需要七份符号文件".into());
    }
    let mut snapshot = Snapshot::default();
    for (level, file) in files.iter().take(4).enumerate() {
        let mut entries = Vec::new();
        for (line, r) in rows(file, '\u{1}')?.iter().enumerate() {
            let min = if level == 0 { 8 } else { 10 };
            if r.len() < min || r[0].parse::<u64>().is_err() {
                return Err(format!(
                    "{} 第 {} 行字段不匹配：{} 列",
                    FILES[level],
                    line + 1,
                    r.len()
                ));
            }
            let user_level = if level == 0 { 3 } else { 4 };
            let access = r[user_level]
                .parse::<u32>()
                .map_err(|_| format!("{} 第 {} 行 UserLevel 无效", FILES[level], line + 1))?;
            if access >= 4 {
                continue;
            }
            let name = clean(&r[if level == 0 { 1 } else { 2 }]);
            if name.is_empty() {
                continue;
            }
            entries.push(Entry {
                name,
                parent: if level == 0 {
                    String::new()
                } else {
                    clean(&r[1])
                },
                help: clean(&r[if level == 0 { 2 } else { 3 }]),
                alias: if level == 0 {
                    clean(&r[4])
                } else {
                    String::new()
                },
                gate: if level == 2 {
                    clean(&r[5])
                } else {
                    String::new()
                },
            });
        }
        snapshot.levels.push(entries);
    }
    for (index, file) in files.iter().enumerate().skip(4) {
        for (line, r) in rows(file, '\t')?.iter().enumerate() {
            if r.len() < 4 {
                return Err(format!("{} 第 {} 行字段不足", FILES[index], line + 1));
            }
            let entry = Entry {
                name: clean(&r[2]),
                parent: clean(&r[1]),
                alias: if index == 5 {
                    clean(&r[0])
                } else {
                    String::new()
                },
                help: if index == 4 {
                    format!("{}: {} = {}", r[1], r[2], r[3])
                } else {
                    String::new()
                },
                ..Default::default()
            };
            match index {
                4 => {
                    if ["ptr", "global", "csglobal", "userdefine"]
                        .contains(&r[1].to_lowercase().as_str())
                    {
                        snapshot.globals.push(entry);
                    }
                }
                5 => {
                    if ["prog", "subprog", "plc"].contains(&r[1].to_lowercase().as_str()) {
                        snapshot.programs.push(entry);
                    }
                }
                6 => snapshot.locals.push(entry),
                _ => {}
            }
        }
    }
    if snapshot.levels[0].is_empty() {
        return Err("控制器命令表为空".into());
    }
    Ok(snapshot)
}
pub fn fetch(sdk: &Sdk, mut progress: impl FnMut(&str)) -> Result<Snapshot, String> {
    let mut files = Vec::new();
    for file in FILES {
        progress(file);
        let command = format!("cat -- /var/ftp/usrflash/Database/{file}");
        files.push(sdk.shell(&command, 30000)?);
    }
    parse(&files)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn escaped_newline() {
        assert_eq!(
            rows("1\u{1}a\\\r\nb\u{1}x\r\n", '\u{1}').unwrap(),
            vec![vec!["1", "a\nb", "x"]]
        );
    }
    #[test]
    fn escapes() {
        assert_eq!(
            rows("1\t\\N\tA\\tB\tC\\\\D\n", '\t').unwrap(),
            vec![vec!["1", "", "A\tB", "C\\D"]]
        );
    }
    #[test]
    fn invalid_shape() {
        assert!(parse(&vec!["bad".into(); 7]).is_err());
    }
}
