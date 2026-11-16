use crate::{
    completion::Snapshot,
    sdk::{Connection, Sdk},
    symbols,
};
use std::{
    path::PathBuf,
    sync::mpsc::{self, Receiver, Sender},
    time::Instant,
};
pub enum Task {
    Connect(Connection, Option<u8>),
    Disconnect,
    Command(String, u32),
    Refresh,
    Shutdown,
}
pub enum Event {
    Connected(String),
    Disconnected,
    Response(String, f64),
    Error(String),
    Suggestions(Snapshot),
    Message(String),
    Done,
}
pub fn spawn(
    library: PathBuf,
) -> (
    Sender<Task>,
    Receiver<Event>,
    std::thread::JoinHandle<()>,
    std::sync::Arc<std::sync::atomic::AtomicBool>,
) {
    let (tx, rx) = mpsc::channel();
    let (events, output) = mpsc::channel();
    let stop = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
    let worker_stop = stop.clone();
    let join = std::thread::spawn(move || {
        let mut sdk: Option<Sdk> = None;
        while let Ok(task) = rx.recv() {
            let stopping = matches!(task, Task::Shutdown);
            let result: Result<(), String> = (|| {
                match task {
                    Task::Connect(c, echo) => {
                        if let Some(s) = &mut sdk {
                            s.close()?;
                        }
                        sdk = None;
                        let mut s = Sdk::load(&library)?;
                        s.stop = worker_stop.clone();
                        s.connect(&c)?;
                        // Keep a successfully authenticated client even if echo setup fails.
                        let host = c.host.clone();
                        sdk = Some(s);
                        events.send(Event::Connected(host)).ok();
                        if let Some(echo) = echo {
                            sdk.as_ref()
                                .unwrap()
                                .command(&format!("echo{echo}"), 5000)?;
                        }
                        let data = symbols::fetch(sdk.as_ref().unwrap(), |file| {
                            events.send(Event::Message(format!("正在读取 {file}"))).ok();
                        })?;
                        events.send(Event::Suggestions(data)).ok();
                    }
                    Task::Disconnect | Task::Shutdown => {
                        if let Some(s) = &mut sdk {
                            s.close()?;
                        }
                        sdk = None;
                        events.send(Event::Disconnected).ok();
                    }
                    Task::Command(text, timeout) => {
                        let s = sdk.as_ref().ok_or("尚未连接设备")?;
                        let start = Instant::now();
                        let lower = text.trim().to_ascii_lowercase();
                        let response = if lower == "save" || lower == "sav" {
                            let echo = s.command("echo", 5000)?;
                            let echo = echo
                                .trim()
                                .trim_start_matches("echo=")
                                .trim()
                                .parse::<u8>()
                                .unwrap_or(0);
                            s.command("echo0", 5000)?;
                            let result = s.command(&text, timeout.max(120000));
                            let restore = s.command(&format!("echo{echo}"), 5000);
                            match (result, restore) {
                                (Err(e), _) => return Err(e),
                                (Ok(_), Err(e)) => {
                                    return Err(format!("保存完成，但恢复 echo 失败：{e}"));
                                }
                                (Ok(r), Ok(_)) => r,
                            }
                        } else {
                            s.command(&text, timeout)?
                        };
                        events
                            .send(Event::Response(
                                response,
                                start.elapsed().as_secs_f64() * 1000.0,
                            ))
                            .ok();
                    }
                    Task::Refresh => {
                        let s = sdk.as_ref().ok_or("请先连接设备")?;
                        let data = symbols::fetch(s, |file| {
                            events.send(Event::Message(format!("正在读取 {file}"))).ok();
                        })?;
                        events.send(Event::Suggestions(data)).ok();
                        events
                            .send(Event::Message("控制器提示信息已加载".into()))
                            .ok();
                    }
                }
                Ok(())
            })();
            if let Err(e) = result {
                events.send(Event::Error(e)).ok();
            }
            events.send(Event::Done).ok();
            if stopping {
                break;
            }
        }
    });
    (tx, output, join, stop)
}
