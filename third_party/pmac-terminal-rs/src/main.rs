#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
use eframe::egui::{self, Color32, Key, RichText};
use model::{Kind, Settings, Transcript};
use pmac_terminal_core::{completion, model, sdk, symbols, worker};
use std::{
    path::{Path, PathBuf},
    sync::mpsc::{Receiver, Sender},
    time::{Duration, Instant},
};
use worker::{Event, Task};
fn data_dir() -> PathBuf {
    std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(std::env::temp_dir)
        .join("PmacTerminal")
}
fn library_path() -> PathBuf {
    if let Some(p) = std::env::var_os("PMAC_LIBRARY") {
        return PathBuf::from(p);
    }
    let exe = std::env::current_exe().unwrap_or_default();
    let sibling = exe.parent().unwrap_or(Path::new(".")).join("pmac.dll");
    if sibling.exists() {
        sibling
    } else {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("vendor/pmac/native/pmac.dll")
    }
}
struct Terminal {
    stop: std::sync::Arc<std::sync::atomic::AtomicBool>,
    settings: Settings,
    password: String,
    insecure: bool,
    tx: Sender<Task>,
    rx: Receiver<Event>,
    join: Option<std::thread::JoinHandle<()>>,
    transcript: Transcript,
    input: String,
    snapshot: completion::Snapshot,
    selected: usize,
    popup: bool,
    history_index: Option<usize>,
    draft: String,
    connected: bool,
    busy: bool,
    status: String,
    settings_open: bool,
    export_path: String,
    log_path: String,
    logging: bool,
    last_activity: Instant,
    close_requested: bool,
    focus_input: bool,
    macro_mode: String,
    search: String,
    cursor: usize,
}
impl Terminal {
    fn new(cc: &eframe::CreationContext<'_>) -> Self {
        let mut settings: Settings = std::fs::read_to_string(data_dir().join("settings.json"))
            .ok()
            .and_then(|s| serde_json::from_str(&s).ok())
            .unwrap_or_default();
        if settings.palette_version == 0 { settings.apply_original_palette(); }
        let mut fonts = egui::FontDefinitions::default();
        for file in ["C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"] {
            if let Ok(bytes) = std::fs::read(file) {
                fonts
                    .font_data
                    .insert("Chinese".into(), egui::FontData::from_owned(bytes).into());
                fonts
                    .families
                    .get_mut(&egui::FontFamily::Proportional)
                    .unwrap()
                    .insert(0, "Chinese".into());
                fonts
                    .families
                    .get_mut(&egui::FontFamily::Monospace)
                    .unwrap()
                    .push("Chinese".into());
                break;
            }
        }
        cc.egui_ctx.set_fonts(fonts);
        let (tx, rx, join, stop) = worker::spawn(library_path());
        Self {
            stop,
            settings,
            password: String::new(),
            insecure: true,
            tx,
            rx,
            join: Some(join),
            transcript: Transcript::default(),
            input: String::new(),
            snapshot: Default::default(),
            selected: 0,
            popup: false,
            history_index: None,
            draft: String::new(),
            connected: false,
            busy: false,
            status: "未连接".into(),
            settings_open: false,
            export_path: data_dir().join("terminal.txt").display().to_string(),
            log_path: data_dir().join("session.log").display().to_string(),
            logging: false,
            last_activity: Instant::now(),
            close_requested: false,
            focus_input: true,
            macro_mode: String::new(),
            search: String::new(),
            cursor: 0,
        }
    }
    fn append(&mut self, text: impl Into<String>, kind: Kind) {
        let text = text.into();
        if self.logging {
            use std::io::Write;
            let result = std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(&self.log_path)
                .and_then(|mut f| writeln!(f, "{text}"));
            if let Err(e) = result {
                self.logging = false;
                self.status = format!("日志写入失败：{e}");
            }
        }
        self.transcript.append(text, kind, self.settings.max_chars);
    }
    fn task(&mut self, task: Task) {
        match self.tx.send(task) {
            Ok(()) => {
                self.busy = true;
                self.last_activity = Instant::now();
            }
            Err(e) => self.append(e.to_string(), Kind::Error),
        }
    }
    fn send(&mut self, text: String) {
        if !self.connected || self.busy {
            return;
        }
        let text = text.trim_end().to_owned();
        if text.is_empty() {
            return;
        }
        model::remember(&mut self.settings.history, &text);
        self.history_index = None;
        self.append(text.clone(), Kind::Command);
        self.popup = false;
        let low = text.to_ascii_lowercase();
        if low.starts_with("macrostation") {
            self.macro_mode = if low.contains("close") {
                String::new()
            } else {
                text.clone()
            };
        }
        self.task(Task::Command(text, self.settings.timeout));
        self.input.clear();
        self.cursor = 0;
        self.focus_input = true;
    }
    fn connect(&mut self) {
        self.snapshot = Default::default();
        self.macro_mode.clear();
        let c = sdk::Connection {
            host: self.settings.host.trim().into(),
            port: self.settings.port,
            user: self.settings.user.clone(),
            password: self.password.clone(),
            known_hosts: self.settings.known_hosts.clone(),
            pinned: self.settings.pinned.clone(),
            private_key: self.settings.private_key.clone(),
            insecure: self.insecure,
        };
        self.status = "正在连接…".into();
        self.task(Task::Connect(c, Some(self.settings.echo)));
    }
    fn events(&mut self) {
        while let Ok(event) = self.rx.try_recv() {
            match event {
                Event::Connected(host) => {
                    self.connected = true;
                    self.status = format!("已连接 {host}");
                    self.append(format!("SSH / GPASCII 已连接 {host}"), Kind::Message);
                }
                Event::Disconnected => {
                    self.connected = false;
                    self.snapshot = Default::default();
                    self.status = "未连接".into();
                }
                Event::Response(text, ms) => {
                    for line in text.lines() {
                        self.append(
                            line,
                            if line.to_ascii_lowercase().contains("error #") {
                                Kind::Error
                            } else {
                                Kind::Response
                            },
                        );
                    }
                    if self.settings.timing {
                        self.append(format!("响应时间 {ms:.3} ms"), Kind::Message);
                    }
                }
                Event::Error(e) => {
                    self.status = e.clone();
                    self.append(e, Kind::Error);
                }
                Event::Suggestions(data) => {
                    let count = data.levels.iter().map(Vec::len).sum::<usize>()
                        + data.globals.len()
                        + data.programs.len()
                        + data.locals.len();
                    self.snapshot = data;
                    self.status = format!("已连接 · {count} 条提示");
                    self.append(format!("已从控制器加载 {count} 条提示"), Kind::Message);
                }
                Event::Message(s) => {
                    self.status = s;
                }
                Event::Done => {
                    self.busy = false;
                    self.last_activity = Instant::now();
                }
            }
        }
    }
    fn history(&mut self, older: bool) {
        if self.settings.history.is_empty() {
            return;
        }
        let next = match (self.history_index, older) {
            (None, true) => {
                self.draft = self.input.clone();
                Some(0)
            }
            (Some(i), true) => Some((i + 1).min(self.settings.history.len() - 1)),
            (Some(0), false) => None,
            (Some(i), false) => Some(i - 1),
            (None, false) => None,
        };
        self.history_index = next;
        self.input = next
            .map(|i| self.settings.history[i].clone())
            .unwrap_or_else(|| self.draft.clone());
        self.cursor = self.input.chars().count();
        self.focus_input = true;
    }
    fn accept(&mut self, name: &str) {
        let cursor = self
            .input
            .char_indices()
            .nth(self.cursor)
            .map(|(i, _)| i)
            .unwrap_or(self.input.len());
        let before = &self.input[..cursor];
        let token = completion::token_start(before);
        let start = before[token..]
            .rfind('.')
            .map(|i| token + i + 1)
            .unwrap_or(token);
        let end = self.input[cursor..]
            .char_indices()
            .find(|(_, c)| c.is_whitespace() || ".=+-*/(),;".contains(*c))
            .map(|(i, _)| cursor + i)
            .unwrap_or(self.input.len());
        self.input.replace_range(start..end, name);
        self.cursor = self.input[..start].chars().count() + completion::insertion_cursor(name);
        self.popup = false;
        self.focus_input = true;
    }
    fn save_settings(&mut self) {
        let result = (|| {
            std::fs::create_dir_all(data_dir())?;
            let bytes = serde_json::to_vec_pretty(&self.settings)?;
            std::fs::write(data_dir().join("settings.json"), bytes)?;
            Ok::<_, Box<dyn std::error::Error>>(())
        })();
        if let Err(e) = result {
            self.append(format!("保存设置失败：{e}"), Kind::Error);
        }
    }
    fn export(&mut self) {
        match std::fs::write(&self.export_path, self.transcript.text()) {
            Ok(()) => self.status = format!("已导出 {}", self.export_path),
            Err(e) => self.append(format!("导出失败：{e}"), Kind::Error),
        }
    }
}
impl eframe::App for Terminal {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.events();
        ctx.request_repaint_after(Duration::from_millis(100));
        ctx.set_visuals(if self.settings.dark {
            egui::Visuals::dark()
        } else {
            egui::Visuals::light()
        });
        if ctx.input(|i| i.viewport().close_requested()) && !self.close_requested {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.close_requested = true;
            self.stop.store(true, std::sync::atomic::Ordering::Relaxed);
            self.task(Task::Shutdown);
            self.status = "正在释放通信资源…".into();
            self.save_settings();
        }
        if self.close_requested && self.join.as_ref().is_some_and(|j| j.is_finished()) {
            if let Some(j) = self.join.take() {
                let _ = j.join();
            }
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
            return;
        }
        if self.close_requested {
            return;
        }
        if self.connected
            && !self.busy
            && self.settings.heartbeat
            && self.last_activity.elapsed() > Duration::from_secs(5)
        {
            self.task(Task::Command("Sys.ServoTime".into(), 5000));
        }
        egui::TopBottomPanel::top("toolbar").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.label(RichText::new("PMAC TERMINAL").strong().size(18.));
                ui.separator();
                ui.label("地址");
                ui.add_enabled(
                    !self.busy,
                    egui::TextEdit::singleline(&mut self.settings.host).desired_width(135.),
                );
                if ui
                    .add_enabled(
                        !self.busy,
                        egui::Button::new(if self.connected {
                            "重新连接"
                        } else {
                            "连接"
                        }),
                    )
                    .clicked()
                {
                    self.connect();
                }
                if ui
                    .add_enabled(self.connected && !self.busy, egui::Button::new("断开"))
                    .clicked()
                {
                    self.task(Task::Disconnect);
                }
                if ui
                    .add_enabled(self.connected && !self.busy, egui::Button::new("刷新提示"))
                    .clicked()
                {
                    self.task(Task::Refresh);
                }
                if self.busy {
                    ui.spinner();
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.button("设置").clicked() {
                        self.settings_open = true;
                    }
                    if ui.button("新窗口").clicked() {
                        if let Ok(exe) = std::env::current_exe() {
                            if let Err(e) = std::process::Command::new(exe).spawn() {
                                self.append(e.to_string(), Kind::Error);
                            }
                        }
                    }
                });
            });
            ui.horizontal(|ui| {
                ui.label("用户");
                ui.add(egui::TextEdit::singleline(&mut self.settings.user).desired_width(80.));
                ui.label("密码");
                ui.add(
                    egui::TextEdit::singleline(&mut self.password)
                        .password(true)
                        .desired_width(145.),
                );
                ui.checkbox(&mut self.insecure, "允许未验证的主机密钥")
                    .on_hover_text("仅本次运行有效；也可在设置中配置 known_hosts 或 SHA256 指纹。");
                ui.separator();
                if ui.button("清空响应").clicked() {
                    self.transcript.clear();
                }
                if ui.button("复制响应").clicked() {
                    ctx.copy_text(self.transcript.text());
                }
            });
        });
        egui::TopBottomPanel::bottom("status").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.colored_label(
                    if self.connected {
                        Color32::from_rgb(40, 145, 100)
                    } else {
                        Color32::GRAY
                    },
                    if self.connected { "●" } else { "○" },
                );
                ui.label(&self.status);
                if !self.macro_mode.is_empty() {
                    ui.separator();
                    ui.label(&self.macro_mode);
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.label("SDK 2.0.5 · Windows x64");
                });
            });
        });
        egui::TopBottomPanel::bottom("command")
            .resizable(true)
            .default_height(145.)
            .min_height(100.)
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    ui.strong("命令");
                    ui.label(
                        "Enter 接受提示/发送 · Shift+Enter 换行 · Tab 补全 · ↑↓ 历史 / 候选 · Esc 关闭提示",
                    );
                });
                let id = ui.make_persistent_id("command_edit");
                let focused = ui.memory(|m| m.has_focus(id));
                let cursor_byte = self
                    .input
                    .char_indices()
                    .nth(self.cursor)
                    .map(|(i, _)| i)
                    .unwrap_or(self.input.len());
                let suggestions = if self.popup {
                    self.snapshot.suggest(&self.input[..cursor_byte])
                } else {
                    Vec::new()
                };
                self.selected = self.selected.min(suggestions.len().saturating_sub(1));
                let mut send = false;
                let mut replacement = None;
                let mut history = None;
                if focused {
                    ctx.input_mut(|i| {
                        if i.consume_key(egui::Modifiers::NONE, Key::Escape) {
                            self.popup = false;
                        }
                        if i.consume_key(egui::Modifiers::NONE, Key::Tab) && self.popup {
                            replacement = suggestions.get(self.selected).map(|s| s.name.clone());
                        }
                        if i.consume_key(egui::Modifiers::NONE, Key::ArrowUp) {
                            if self.popup && !suggestions.is_empty() {
                                self.selected = self.selected.saturating_sub(1);
                            } else if self.settings.doskey {
                                history = Some(true);
                            }
                        }
                        if i.consume_key(egui::Modifiers::NONE, Key::ArrowDown) {
                            if self.popup && !suggestions.is_empty() {
                                self.selected = (self.selected + 1).min(suggestions.len() - 1);
                            } else if self.settings.doskey {
                                history = Some(false);
                            }
                        }
                        if i.consume_key(egui::Modifiers::NONE, Key::Enter) {
                            if self.popup && !suggestions.is_empty() {
                                replacement = suggestions.get(self.selected).map(|s| s.name.clone());
                            } else {
                                send = true;
                            }
                        }
                    });
                }
                if let Some(name) = replacement {
                    self.accept(&name);
                }
                if let Some(older) = history {
                    self.history(older);
                }
                if self.focus_input {
                    let mut state =
                        egui::text_edit::TextEditState::load(ctx, id).unwrap_or_default();
                    state
                        .cursor
                        .set_char_range(Some(egui::text::CCursorRange::one(
                            egui::text::CCursor::new(self.cursor),
                        )));
                    state.store(ctx, id);
                }
                let edit = egui::TextEdit::multiline(&mut self.input)
                    .id(id)
                    .font(egui::FontId::monospace(self.settings.font_size))
                    .text_color(Color32::BLACK)
                    .background_color(Color32::WHITE)
                    .char_limit(65536)
                    .desired_rows(3)
                    .desired_width(f32::INFINITY)
                    .lock_focus(true);
                let output = edit.show(ui);
                if self.focus_input {
                    output.response.request_focus();
                    self.focus_input = false;
                }
                if let Some(range) = output.cursor_range {
                    self.cursor = range.primary.index;
                }
                if output.response.changed() {
                    self.popup =
                        !self.input.is_empty() && !self.input.ends_with(char::is_whitespace);
                    self.selected = 0;
                }
                ui.horizontal(|ui| {
                    ui.checkbox(&mut self.settings.timing, "响应时间");
                    ui.checkbox(&mut self.settings.doskey, "命令历史");
                });
                if send {
                    self.send(self.input.clone());
                }
                if self.popup && !suggestions.is_empty() {
                    let pos = output.response.rect.left_top() - egui::vec2(0., 230.);
                    egui::Area::new("completion".into())
                        .order(egui::Order::Foreground)
                        .fixed_pos(pos)
                        .show(ctx, |ui| {
                            egui::Frame::popup(ui.style()).show(ui, |ui| {
                                ui.set_width(430.);
                                ui.set_max_height(220.);
                                egui::ScrollArea::vertical()
                                    .max_height(160.)
                                    .show(ui, |ui| {
                                        for (i, s) in suggestions.iter().enumerate() {
                                            let r =
                                                ui.selectable_label(i == self.selected, &s.name);
                                            if i == self.selected {
                                                r.scroll_to_me(Some(egui::Align::Center));
                                            }
                                            if r.clicked() {
                                                self.accept(&s.name);
                                            }
                                            r.on_hover_text(&s.help);
                                        }
                                    });
                                ui.separator();
                                if let Some(s) = suggestions.get(self.selected) {
                                    ui.label(&s.help);
                                }
                            });
                        });
                }
            });
        egui::CentralPanel::default().frame(egui::Frame::central_panel(&ctx.style()).fill(Color32::from_rgb(0, 0, 128))).show(ctx, |ui| {
            ui.visuals_mut().override_text_color = Some(Color32::WHITE);
            ui.horizontal(|ui| {
                ui.strong("响应");
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.add(
                        egui::TextEdit::singleline(&mut self.search)
                            .hint_text("查找响应")
                            .text_color(Color32::BLACK)
                            .desired_width(200.),
                    );
                });
            });
            let height = ui.available_height();
            egui::ScrollArea::both()
                .stick_to_bottom(true)
                .auto_shrink([false, false])
                .show(ui, |ui| {
                    ui.set_min_height(height);
                    for line in &self.transcript.lines {
                        if !self.search.is_empty()
                            && !line
                                .text
                                .to_lowercase()
                                .contains(&self.search.to_lowercase())
                        {
                            continue;
                        }
                        let rgb = match line.kind {
                            Kind::Response => self.settings.response_color,
                            Kind::Command => self.settings.command_color,
                            Kind::Error => self.settings.error_color,
                            Kind::Message => self.settings.message_color,
                        };
                        let color = Color32::from_rgb(rgb[0], rgb[1], rgb[2]);
                        ui.add(
                            egui::Label::new(
                                RichText::new(&line.text)
                                    .monospace()
                                    .size(self.settings.font_size)
                                    .color(color),
                            )
                            .selectable(true),
                        );
                    }
                });
            for dropped in ctx.input(|i| i.raw.dropped_files.clone()) {
                if let Some(path) = dropped.path {
                    match std::fs::read_to_string(path) {
                        Ok(text) => {
                            self.input = text;
                            self.cursor = self.input.chars().count();
                            self.focus_input = true;
                        }
                        Err(e) => self.append(format!("读取命令文件失败：{e}"), Kind::Error),
                    }
                }
            }
        });
        self.settings_ui(ctx);
    }
}
impl Terminal {
    fn settings_ui(&mut self, ctx: &egui::Context) {
        let mut open = self.settings_open;
        let mut save = false;
        egui::Window::new("终端设置").open(&mut open).default_width(540.).resizable(true).show(ctx,|ui|{
            egui::ScrollArea::vertical().max_height(650.).show(ui,|ui|{
                ui.strong("连接");egui::Grid::new("connection_settings").num_columns(2).show(ui,|ui|{
                    ui.label("SSH 端口");ui.add(egui::DragValue::new(&mut self.settings.port).range(1..=65535));ui.end_row();
                    ui.label("known_hosts 文件");ui.text_edit_singleline(&mut self.settings.known_hosts);ui.end_row();
                    ui.label("SHA256 主机指纹");ui.text_edit_singleline(&mut self.settings.pinned);ui.end_row();
                    ui.label("私钥文件（可选）");ui.text_edit_singleline(&mut self.settings.private_key);ui.end_row();
                    ui.label("命令超时 / ms");ui.add(egui::DragValue::new(&mut self.settings.timeout).range(100..=300000));ui.end_row();
                    ui.label("Echo 模式");if ui.add(egui::DragValue::new(&mut self.settings.echo).range(0..=3)).changed()&&self.connected&&!self.busy{self.task(Task::Command(format!("echo{}",self.settings.echo),5000));}ui.end_row();
                });
                ui.checkbox(&mut self.settings.heartbeat,"每 5 秒读取 Sys.ServoTime（心跳）");
                ui.label("连接后从控制器读取提示信息，刷新提示可重新获取。无需数据库服务。");
                ui.separator();ui.strong("显示");ui.checkbox(&mut self.settings.dark,"深色背景");
                ui.horizontal(|ui|{ui.label("字号");ui.add(egui::Slider::new(&mut self.settings.font_size,10.0..=28.0));});
                ui.horizontal(|ui|{ui.label("响应缓冲 / 字节");ui.add(egui::DragValue::new(&mut self.settings.max_chars).range(1000..=10_000_000));});
                egui::Grid::new("colors").num_columns(2).show(ui,|ui|{for (name,color) in [("响应",&mut self.settings.response_color),("已发送命令",&mut self.settings.command_color),("设备错误",&mut self.settings.error_color),("系统消息",&mut self.settings.message_color)]{ui.label(name);ui.color_edit_button_srgb(color);ui.end_row();}});
                ui.separator();ui.strong("日志与导出");
                ui.horizontal(|ui|{ui.label("日志路径");ui.text_edit_singleline(&mut self.log_path);});
                if ui.checkbox(&mut self.logging,"记录会话日志").changed()&&self.logging{if let Some(parent)=Path::new(&self.log_path).parent(){if let Err(e)=std::fs::create_dir_all(parent){self.logging=false;self.append(e.to_string(),Kind::Error);}}}
                ui.horizontal(|ui|{ui.label("导出路径");ui.text_edit_singleline(&mut self.export_path);});
                if ui.button("导出响应为 UTF-8 文本").clicked(){self.export();}
                if ui.button("清空历史").clicked(){self.settings.history.clear();self.history_index=None;}
                ui.separator();ui.label("密码和允许未验证主机密钥选项不保存。命令历史、界面和连接设置保存到用户目录。");
                if ui.button("保存设置").clicked(){save=true;}
            });
        });
        self.settings_open = open;
        if save {
            self.save_settings();
        }
    }
}
fn read_only_test() -> Result<(), String> {
    let mut sdk = sdk::Sdk::load(&library_path())?;
    println!("SDK: {:?}", sdk.info()?);
    let host = std::env::var("PMAC_HOST").unwrap_or_else(|_| "192.168.0.200".into());
    let password =
        std::env::var("PMAC_PASSWORD").map_err(|_| "请设置 PMAC_PASSWORD（不写入文件）")?;
    let c = sdk::Connection {
        host,
        user: std::env::var("PMAC_USER").unwrap_or_else(|_| "root".into()),
        password,
        port: 22,
        known_hosts: std::env::var("PMAC_KNOWN_HOSTS").unwrap_or_default(),
        pinned: std::env::var("PMAC_PINNED_SHA256").unwrap_or_default(),
        private_key: String::new(),
        insecure: std::env::args().any(|a| a == "--allow-unverified"),
    };
    sdk.connect(&c)?;
    println!(
        "READ Sys.ServoTime: {}",
        sdk.command("Sys.ServoTime", 5000)?
    );
    let snapshot = symbols::fetch(&sdk, |file| println!("Reading {file}"))?;
    println!(
        "Levels {:?}, globals {}, programs {}, locals {}",
        snapshot.levels.iter().map(Vec::len).collect::<Vec<_>>(),
        snapshot.globals.len(),
        snapshot.programs.len(),
        snapshot.locals.len()
    );
    for (prefix, expected) in [
        ("Motor[1].Servo", "ServoCtrl"),
        ("Sys.Servo", "ServoTime"),
        ("Gate3[0].Chan[0].", "EncCtrl"),
    ] {
        let suggestions = snapshot.suggest(prefix);
        if !suggestions.iter().any(|s| s.name == expected) {
            return Err(format!("补全验收失败：{prefix} 缺少 {expected}"));
        }
        println!(
            "PASS {prefix} -> {expected} ({} candidates)",
            suggestions.len()
        );
    }
    let names = snapshot.suggest("Motor[1].Servo");
    if names.iter().all(|s| s.help.is_empty()) {
        return Err("成员帮助为空".into());
    }
    sdk.close()?;
    println!("PASS resources closed; no motion/save/reset commands sent");
    Ok(())
}
fn main() -> eframe::Result {
    if std::env::args().any(|a| a == "--check" || a == "--read-only-test") {
        let result = if std::env::args().any(|a| a == "--read-only-test") {
            read_only_test()
        } else {
            sdk::Sdk::load(&library_path()).and_then(|mut s| {
                println!("SDK {:?}", s.info()?);
                s.close()
            })
        };
        if let Err(e) = result {
            eprintln!("{e}");
            std::process::exit(1);
        }
        return Ok(());
    }
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("PMAC Terminal")
            .with_inner_size([1120., 760.])
            .with_min_inner_size([880., 560.]),
        ..Default::default()
    };
    eframe::run_native(
        "PMAC Terminal",
        options,
        Box::new(|cc| Ok(Box::new(Terminal::new(cc)))),
    )
}
