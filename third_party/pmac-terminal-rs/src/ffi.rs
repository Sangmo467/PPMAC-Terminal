use crate::{
    completion::Snapshot,
    sdk::Connection,
    worker::{self, Event, Task},
};
use serde::Deserialize;
use serde_json::json;
use std::{
    collections::{HashMap, VecDeque},
    panic::{AssertUnwindSafe, catch_unwind},
    path::PathBuf,
    sync::mpsc::{Receiver, Sender},
    sync::{
        Arc, Mutex, OnceLock,
        atomic::{AtomicU64, Ordering},
    },
};
const OK: i32 = 0;
const PENDING: i32 = 1;
const INVALID: i32 = -1;
const HANDLE: i32 = -2;
const BUFFER: i32 = -3;
const BUSY: i32 = -4;
const INTERNAL: i32 = -5;
const DISCONNECTED: i32 = -6;
struct State {
    rx: Receiver<Event>,
    events: VecDeque<String>,
    event_bytes: usize,
    snapshot: Snapshot,
    connected: bool,
    busy: bool,
    closed: bool,
    last_error: String,
}
struct Session {
    stop: Arc<std::sync::atomic::AtomicBool>,
    tx: Sender<Task>,
    state: Mutex<State>,
    join: Mutex<Option<std::thread::JoinHandle<()>>>,
}
static SESSIONS: OnceLock<Mutex<HashMap<u64, Arc<Session>>>> = OnceLock::new();
static NEXT: AtomicU64 = AtomicU64::new(1);
fn sessions() -> &'static Mutex<HashMap<u64, Arc<Session>>> {
    SESSIONS.get_or_init(|| Mutex::new(HashMap::new()))
}
fn session(id: u64) -> Result<Arc<Session>, i32> {
    sessions()
        .lock()
        .map_err(|_| INTERNAL)?
        .get(&id)
        .cloned()
        .ok_or(HANDLE)
}
fn guard(f: impl FnOnce() -> Result<i32, i32>) -> i32 {
    catch_unwind(AssertUnwindSafe(f))
        .unwrap_or(Err(INTERNAL))
        .unwrap_or_else(|e| e)
}
unsafe fn text<'a>(data: *const u8, len: usize) -> Result<&'a str, i32> {
    if len > 16 * 1024 * 1024 {
        return Err(INVALID);
    }
    if len == 0 {
        return Ok("");
    }
    if data.is_null() {
        return Err(INVALID);
    }
    std::str::from_utf8(unsafe { std::slice::from_raw_parts(data, len) }).map_err(|_| INVALID)
}
unsafe fn copy(
    value: &str,
    out: *mut u8,
    capacity: usize,
    required: *mut usize,
) -> Result<i32, i32> {
    if required.is_null() {
        return Err(INVALID);
    }
    unsafe {
        *required = value.len();
    }
    if capacity < value.len() {
        return Ok(BUFFER);
    }
    if !value.is_empty() {
        if out.is_null() {
            return Err(INVALID);
        }
        unsafe {
            std::ptr::copy_nonoverlapping(value.as_ptr(), out, value.len());
        }
    }
    Ok(OK)
}
impl State {
    fn pump(&mut self) {
        while let Ok(e) = self.rx.try_recv() {
            let data = match e {
                Event::Connected(host) => {
                    self.connected = true;
                    self.snapshot = Snapshot::default();
                    json!({"type":"connected","host":host})
                }
                Event::Disconnected => {
                    self.connected = false;
                    self.snapshot = Snapshot::default();
                    json!({"type":"disconnected"})
                }
                Event::Response(text, elapsed_ms) => {
                    json!({"type":"response","text":text,"elapsed_ms":elapsed_ms})
                }
                Event::Error(text) => {
                    self.last_error = text.clone();
                    json!({"type":"error","text":text})
                }
                Event::Message(text) => json!({"type":"progress","text":text}),
                Event::Suggestions(s) => {
                    let counts: Vec<_> = s.levels.iter().map(Vec::len).collect();
                    let data = json!({"type":"symbols_loaded","levels":counts,"globals":s.globals.len(),"programs":s.programs.len(),"locals":s.locals.len()});
                    self.snapshot = s;
                    data
                }
                Event::Done => {
                    self.busy = false;
                    json!({"type":"ready","connected":self.connected})
                }
            };
            let payload = data.to_string();
            self.event_bytes += payload.len();
            self.events.push_back(payload);
        }
    }
}
fn submit(id: u64, task: Task, requires_connection: bool) -> Result<i32, i32> {
    let s = session(id)?;
    let mut state = s.state.lock().map_err(|_| INTERNAL)?;
    state.pump();
    if state.closed {
        return Err(HANDLE);
    }
    if state.busy {
        return Err(BUSY);
    }
    if requires_connection && !state.connected {
        return Err(DISCONNECTED);
    }
    // Bounded by application backpressure: one operation in flight, and undrained events
    // must be consumed before further submissions after 256 operations.
    if state.events.len() > 4096 || state.event_bytes > 16 * 1024 * 1024 {
        return Err(BUSY);
    }
    s.tx.send(task).map_err(|_| INTERNAL)?;
    state.busy = true;
    Ok(OK)
}
#[derive(Deserialize)]
#[serde(default)]
struct ConnectConfig {
    host: String,
    port: u16,
    username: String,
    password: String,
    known_hosts: String,
    pinned_sha256: String,
    private_key: String,
    accept_any_host_key: bool,
    echo: Option<u8>,
}
impl Default for ConnectConfig {
    fn default() -> Self {
        Self {
            host: String::new(),
            port: 22,
            username: "root".into(),
            password: String::new(),
            known_hosts: String::new(),
            pinned_sha256: String::new(),
            private_key: String::new(),
            accept_any_host_key: true,
            echo: None,
        }
    }
}
/// The caller owns all buffers. No Rust allocations cross this boundary.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmact_create(path: *const u8, path_len: usize, out: *mut u64) -> i32 {
    guard(|| {
        if out.is_null() {
            return Err(INVALID);
        }
        unsafe {
            *out = 0;
        }
        let path = unsafe { text(path, path_len) }?;
        let library = if path.is_empty() {
            std::env::current_exe()
                .map_err(|_| INTERNAL)?
                .parent()
                .ok_or(INTERNAL)?
                .join("pmac.dll")
        } else {
            PathBuf::from(path)
        };
        let (tx, rx, join, stop) = worker::spawn(library);
        let id = NEXT.fetch_add(1, Ordering::Relaxed);
        if id == 0 {
            return Err(INTERNAL);
        }
        let s = Session {
            stop,
            tx,
            state: Mutex::new(State {
                rx,
                events: VecDeque::new(),
                event_bytes: 0,
                snapshot: Default::default(),
                connected: false,
                busy: false,
                closed: false,
                last_error: String::new(),
            }),
            join: Mutex::new(Some(join)),
        };
        sessions()
            .lock()
            .map_err(|_| INTERNAL)?
            .insert(id, Arc::new(s));
        unsafe {
            *out = id;
        }
        Ok(OK)
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmact_connect(id: u64, config: *const u8, len: usize) -> i32 {
    guard(|| {
        let text = unsafe { text(config, len) }?;
        let c: ConnectConfig = serde_json::from_str(text).map_err(|_| INVALID)?;
        if c.host.is_empty() || c.port == 0 || c.echo.is_some_and(|e| e > 3) {
            return Err(INVALID);
        }
        let connection = Connection {
            host: c.host,
            port: c.port,
            user: c.username,
            password: c.password,
            known_hosts: c.known_hosts,
            pinned: c.pinned_sha256,
            private_key: c.private_key,
            insecure: c.accept_any_host_key,
        };
        submit(id, Task::Connect(connection, c.echo), false)
    })
}
#[unsafe(no_mangle)]
pub extern "C" fn pmact_disconnect(id: u64) -> i32 {
    guard(|| submit(id, Task::Disconnect, false))
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmact_send(
    id: u64,
    command: *const u8,
    len: usize,
    timeout_ms: u32,
) -> i32 {
    guard(|| {
        let command = unsafe { text(command, len) }?;
        if command.trim().is_empty() || timeout_ms == 0 {
            return Err(INVALID);
        }
        submit(id, Task::Command(command.into(), timeout_ms), true)
    })
}
#[unsafe(no_mangle)]
pub extern "C" fn pmact_refresh_symbols(id: u64) -> i32 {
    guard(|| submit(id, Task::Refresh, true))
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmact_poll_event(
    id: u64,
    out: *mut u8,
    capacity: usize,
    required: *mut usize,
) -> i32 {
    guard(|| {
        let s = session(id)?;
        let mut state = s.state.lock().map_err(|_| INTERNAL)?;
        state.pump();
        if state.closed {
            return Err(HANDLE);
        }
        let Some(event) = state.events.front() else {
            if required.is_null() {
                return Err(INVALID);
            }
            unsafe {
                *required = 0;
            }
            return Ok(PENDING);
        };
        let status = unsafe { copy(event, out, capacity, required) }?;
        if status == OK {
            if let Some(event) = state.events.pop_front() {
                state.event_bytes -= event.len();
            }
        }
        Ok(status)
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmact_suggest(
    id: u64,
    prefix: *const u8,
    len: usize,
    out: *mut u8,
    capacity: usize,
    required: *mut usize,
) -> i32 {
    guard(|| {
        let prefix = unsafe { text(prefix, len) }?;
        let s = session(id)?;
        let mut state = s.state.lock().map_err(|_| INTERNAL)?;
        state.pump();
        if state.closed {
            return Err(HANDLE);
        }
        let candidates: Vec<_> = state
            .snapshot
            .suggest(prefix)
            .iter()
            .map(|s| json!({"text":s.name,"help":s.help}))
            .collect();
        unsafe {
            copy(
                &serde_json::to_string(&candidates).map_err(|_| INTERNAL)?,
                out,
                capacity,
                required,
            )
        }
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmact_last_error(
    id: u64,
    out: *mut u8,
    capacity: usize,
    required: *mut usize,
) -> i32 {
    guard(|| {
        let s = session(id)?;
        let mut state = s.state.lock().map_err(|_| INTERNAL)?;
        state.pump();
        unsafe { copy(&state.last_error, out, capacity, required) }
    })
}
#[unsafe(no_mangle)]
pub extern "C" fn pmact_destroy(id: u64) -> i32 {
    guard(|| {
        let s = sessions()
            .lock()
            .map_err(|_| INTERNAL)?
            .remove(&id)
            .ok_or(HANDLE)?;
        s.state.lock().map_err(|_| INTERNAL)?.closed = true;
        s.stop.store(true, Ordering::Relaxed);
        s.tx.send(Task::Shutdown).map_err(|_| INTERNAL)?;
        if let Some(join) = s.join.lock().map_err(|_| INTERNAL)?.take() {
            join.join().map_err(|_| INTERNAL)?;
        }
        let mut state = s.state.lock().map_err(|_| INTERNAL)?;
        state.pump();
        if state.connected {
            return Err(INTERNAL);
        }
        Ok(OK)
    })
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn lifecycle_and_buffer() {
        unsafe {
            let mut id = 0;
            assert_eq!(pmact_create(std::ptr::null(), 0, &mut id), 0);
            assert_ne!(id, 0);
            let mut required = 99;
            assert_eq!(
                pmact_poll_event(id, std::ptr::null_mut(), 0, &mut required),
                PENDING
            );
            assert_eq!(required, 0);
            assert_eq!(
                pmact_send(id, b"Sys.ServoTime".as_ptr(), 13, 5000),
                DISCONNECTED
            );
            assert_eq!(
                pmact_suggest(
                    id,
                    std::ptr::null(),
                    0,
                    std::ptr::null_mut(),
                    0,
                    &mut required
                ),
                BUFFER
            );
            assert_eq!(required, 2);
            let mut out = [0; 2];
            assert_eq!(
                pmact_suggest(id, std::ptr::null(), 0, out.as_mut_ptr(), 2, &mut required),
                0
            );
            assert_eq!(&out, b"[]");
            assert_eq!(pmact_destroy(id), 0);
            assert_eq!(pmact_destroy(id), HANDLE);
        }
    }
    #[test]
    fn invalid_arguments() {
        unsafe {
            assert_eq!(
                pmact_create(std::ptr::null(), 0, std::ptr::null_mut()),
                INVALID
            );
            assert_eq!(pmact_connect(0, b"{}".as_ptr(), 2), INVALID);
            assert_eq!(
                pmact_poll_event(0, std::ptr::null_mut(), 0, std::ptr::null_mut()),
                HANDLE
            );
        }
    }
}
#[cfg(test)]
mod concurrency_tests {
    use super::*;
    #[test]
    fn independent_sessions_in_parallel() {
        let threads: Vec<_> = (0..8)
            .map(|_| {
                std::thread::spawn(|| unsafe {
                    let mut id = 0;
                    assert_eq!(pmact_create(std::ptr::null(), 0, &mut id), OK);
                    let mut n = 0;
                    assert_eq!(
                        pmact_suggest(id, b"[\xff".as_ptr(), 2, std::ptr::null_mut(), 0, &mut n),
                        INVALID
                    );
                    for p in ["中文🙂[", "'\"$`\\\0", "Motor[1]..", ""] {
                        let bytes = p.as_bytes();
                        assert_eq!(
                            pmact_suggest(
                                id,
                                bytes.as_ptr(),
                                bytes.len(),
                                std::ptr::null_mut(),
                                0,
                                &mut n
                            ),
                            BUFFER
                        );
                    }
                    assert_eq!(pmact_destroy(id), OK);
                    id
                })
            })
            .collect();
        let mut ids = threads
            .into_iter()
            .map(|t| t.join().unwrap())
            .collect::<Vec<_>>();
        ids.sort();
        ids.dedup();
        assert_eq!(ids.len(), 8);
    }
}

#[cfg(test)] mod connection_default_tests {
    use super::*;
    #[test] fn unverified_hosts_allowed_unless_disabled() {
        let default: ConnectConfig = serde_json::from_str(r#"{"host":"device"}"#).unwrap();
        assert!(default.accept_any_host_key);
        let strict: ConnectConfig = serde_json::from_str(r#"{"host":"device","accept_any_host_key":false}"#).unwrap();
        assert!(!strict.accept_any_host_key);
    }
}
// Import symbol files fetched by a host-owned connection. Never opens a connection.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pmact_set_symbol_files(id: u64, data: *const u8, len: usize) -> i32 {
    guard(|| {
        let input = unsafe { text(data, len) }?;
        let files: Vec<String> = serde_json::from_str(input).map_err(|_| INVALID)?;
        let snapshot = if files.is_empty() { Snapshot::default() } else {
            crate::symbols::parse(&files).map_err(|_| INVALID)?
        };
        let s = session(id)?;
        let mut state = s.state.lock().map_err(|_| INTERNAL)?;
        state.pump();
        if state.closed { return Err(HANDLE); }
        if state.busy { return Err(BUSY); }
        state.snapshot = snapshot;
        Ok(OK)
    })
}

#[cfg(test)]
mod host_symbols_tests {
    use super::*;
    #[test]
    fn shared_connection_index_and_dollar_input() {
        unsafe {
            let mut id = 0;
            assert_eq!(pmact_create(b"unused.dll".as_ptr(), 10, &mut id), OK);
            let files = vec![
                "1Motor[]Motor help0
".to_string(),
                "1Motor[]ActPosPosition help0
".to_string(),
                String::new(), String::new(), String::new(), String::new(), String::new()];
            let json = serde_json::to_vec(&files).unwrap();
            assert_eq!(pmact_set_symbol_files(id, json.as_ptr(), json.len()), OK);
            for prefix in ["Mot", "Motor[1].Act", "$", "$$", "$$$", "中文🙂=$$$"] {
                let mut needed = 0;
                assert_eq!(pmact_suggest(id, prefix.as_ptr(), prefix.len(), std::ptr::null_mut(), 0, &mut needed), BUFFER);
                let mut output = vec![0; needed];
                assert_eq!(pmact_suggest(id, prefix.as_ptr(), prefix.len(), output.as_mut_ptr(), output.len(), &mut needed), OK);
                let result: serde_json::Value = serde_json::from_slice(&output).unwrap();
                if prefix == "Mot" { assert_eq!(result[0]["text"], "Motor[]"); }
                if prefix == "Motor[1].Act" { assert_eq!(result[0]["text"], "ActPos"); }
            }
            assert_eq!(pmact_set_symbol_files(id, b"[]".as_ptr(), 2), OK);
            assert_eq!(pmact_destroy(id), OK);
        }
    }
}