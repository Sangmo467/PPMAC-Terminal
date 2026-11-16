//! ABI declarations mirror vendor/pmac/include/pmac.h (ABI 2001, Windows x64).
use libloading::Library;
use std::{
    mem::{size_of, zeroed},
    path::Path,
    ptr,
};
const ABI: u32 = 2001;
#[repr(C)]
#[derive(Clone, Copy)]
struct Bytes {
    data: *const u8,
    len: usize,
}
impl Bytes {
    fn of(s: &str) -> Self {
        Self {
            data: s.as_ptr(),
            len: s.len(),
        }
    }
}
#[repr(C)]
struct RuntimeConfig {
    size: u32,
    abi: u32,
    workers: u32,
    reserved: u32,
}
#[repr(C)]
struct ClientConfig {
    size: u32,
    abi: u32,
    requests: u32,
    files: u32,
    retained: usize,
}
#[repr(C)]
struct ConnectOptions {
    size: u32,
    abi: u32,
    host: Bytes,
    user: Bytes,
    password: Bytes,
    private_key: Bytes,
    public_key: Bytes,
    passphrase: Bytes,
    known_hosts: Bytes,
    pinned: Bytes,
    port: u16,
    pool: u8,
    reserved0: u8,
    flags: u32,
    probe: u32,
    keepalive: u32,
    keepalive_max: u32,
    reserved: [u64; 4],
}
#[repr(C)]
struct Options {
    size: u32,
    abi: u32,
    timeout: u32,
    flags: u32,
    max_response: usize,
}
#[repr(C)]
#[derive(Default)]
struct Info {
    size: u32,
    abi: u32,
    request: u64,
    client: u64,
    state: u32,
    status: i32,
    certainty: u32,
    reserved: u32,
    epoch: u64,
    elapsed: u64,
    response: usize,
    stderr: usize,
    exit: i32,
    has_exit: u32,
}
#[repr(C)]
#[derive(Default)]
struct ErrorInfo {
    size: u32,
    abi: u32,
    status: i32,
    certainty: u32,
    request: u64,
    epoch: u64,
}
#[repr(C)]
#[derive(Default, Debug)]
pub struct RuntimeInfo {
    pub size: u32,
    pub major: u32,
    pub minor: u32,
    pub version: u32,
    pub features: u32,
    pub width: u32,
}
#[derive(Clone)]
pub struct Connection {
    pub host: String,
    pub port: u16,
    pub user: String,
    pub password: String,
    pub known_hosts: String,
    pub pinned: String,
    pub private_key: String,
    pub insecure: bool,
}
pub struct Sdk {
    lib: std::mem::ManuallyDrop<Library>,
    runtime: u64,
    client: u64,
    pub stop: std::sync::Arc<std::sync::atomic::AtomicBool>,
}
// The instance is created, used and destroyed exclusively on the communication worker.
impl Sdk {
    pub fn load(path: &Path) -> Result<Self, String> {
        let lib =
            unsafe { Library::new(path) }.map_err(|e| format!("加载 {}: {e}", path.display()))?;
        let mut s = Self {
            lib: std::mem::ManuallyDrop::new(lib),
            runtime: 0,
            client: 0,
            stop: Default::default(),
        };
        let info = s.info()?;
        if info.major != 2 || info.width != 64 || info.version != 20005 {
            return Err(format!("需要 PMAC 2.0.5 x64，实际 {info:?}"));
        }
        let rc = RuntimeConfig {
            size: size_of::<RuntimeConfig>() as u32,
            abi: ABI,
            workers: 2,
            reserved: 0,
        };
        let status = unsafe {
            (s.symbol::<unsafe extern "C" fn(*const RuntimeConfig, *mut u64) -> i32>(
                b"pmac_runtime_create\0",
            )?)(&rc, &mut s.runtime)
        };
        s.check(status, 0)?;
        let cc = ClientConfig {
            size: size_of::<ClientConfig>() as u32,
            abi: ABI,
            requests: 64,
            files: 2,
            retained: 32 * 1024 * 1024,
        };
        let status = unsafe {
            (s.symbol::<unsafe extern "C" fn(u64, *const ClientConfig, *mut u64) -> i32>(
                b"pmac_client_create\0",
            )?)(s.runtime, &cc, &mut s.client)
        };
        s.check(status, 0)?;
        Ok(s)
    }
    fn symbol<T: Copy>(&self, name: &[u8]) -> Result<T, String> {
        unsafe {
            self.lib
                .get::<T>(name)
                .map(|v| *v)
                .map_err(|e| e.to_string())
        }
    }
    pub fn info(&self) -> Result<RuntimeInfo, String> {
        let mut info = RuntimeInfo::default();
        let status = unsafe {
            (self.symbol::<unsafe extern "C" fn(*mut RuntimeInfo) -> i32>(
                b"pmac_get_runtime_info\0",
            )?)(&mut info)
        };
        self.check(status, 0)?;
        Ok(info)
    }
    fn check(&self, status: i32, request: u64) -> Result<(), String> {
        if status == 0 {
            return Ok(());
        }
        let mut info = ErrorInfo::default();
        let mut required = 0;
        type CopyError =
            unsafe extern "C" fn(u64, *mut ErrorInfo, *mut u8, usize, *mut usize) -> i32;
        type LastError = unsafe extern "C" fn(*mut ErrorInfo, *mut u8, usize, *mut usize) -> i32;
        let mut body = Vec::new();
        unsafe {
            if request != 0 {
                if let Ok(f) = self.symbol::<CopyError>(b"pmac_request_copy_error\0") {
                    f(request, &mut info, ptr::null_mut(), 0, &mut required);
                    if required <= 4 * 1024 * 1024 {
                        body.resize(required, 0);
                        f(
                            request,
                            &mut info,
                            body.as_mut_ptr(),
                            body.len(),
                            &mut required,
                        );
                    }
                }
            } else if let Ok(f) = self.symbol::<LastError>(b"pmac_copy_last_error\0") {
                f(&mut info, ptr::null_mut(), 0, &mut required);
                if required <= 4 * 1024 * 1024 {
                    body.resize(required, 0);
                    f(&mut info, body.as_mut_ptr(), body.len(), &mut required);
                }
            }
        }
        Err(format!(
            "PMAC status={status}, certainty={}, request={}: {}",
            info.certainty,
            info.request,
            String::from_utf8_lossy(&body)
        ))
    }
    fn finish(&self, request: u64, status: i32) -> Result<String, String> {
        self.check(status, 0)?;
        // A request is submitted once. Pending waits never resubmit a command.
        let result = (|| {
            let wait = self.symbol::<unsafe extern "C" fn(u64, u32, *mut Info) -> i32>(
                b"pmac_request_wait\0",
            )?;
            let mut info = Info::default();
            let mut cancelled = false;
            loop {
                if !cancelled && self.stop.load(std::sync::atomic::Ordering::Relaxed) {
                    unsafe {
                        (self.symbol::<unsafe extern "C" fn(u64) -> i32>(
                            b"pmac_request_cancel\0",
                        )?)(request);
                    }
                    cancelled = true;
                }
                let status = unsafe { wait(request, 250, &mut info) };
                if status == -6 {
                    continue;
                }
                self.check(status, request)?;
                break;
            }
            let copy = self
                .symbol::<unsafe extern "C" fn(u64, *mut u8, usize, *mut usize) -> i32>(
                    b"pmac_request_copy_response\0",
                )?;
            let mut n = 0;
            let status = unsafe { copy(request, ptr::null_mut(), 0, &mut n) };
            if status != 0 && status != -11 {
                self.check(status, request)?;
            }
            if n > 32 * 1024 * 1024 {
                return Err("响应超过 32 MiB".into());
            }
            let mut data = vec![0; n];
            let status = unsafe { copy(request, data.as_mut_ptr(), data.len(), &mut n) };
            self.check(status, request)?;
            data.truncate(n);
            Ok(String::from_utf8_lossy(&data).replace('\u{6}', ""))
        })();
        let release = unsafe {
            (self.symbol::<unsafe extern "C" fn(u64) -> i32>(b"pmac_request_release\0")?)(request)
        };
        match result {
            Ok(text) => {
                self.check(release, 0)?;
                Ok(text)
            }
            Err(e) => Err(e),
        }
    }
    pub fn connect(&self, c: &Connection) -> Result<(), String> {
        if !c.insecure && c.known_hosts.is_empty() && c.pinned.is_empty() {
            return Err("请指定 known_hosts 或 SHA256 主机指纹".into());
        }
        let mut opts: ConnectOptions = unsafe { zeroed() };
        opts.size = size_of::<ConnectOptions>() as u32;
        opts.abi = ABI;
        opts.host = Bytes::of(&c.host);
        opts.user = Bytes::of(&c.user);
        opts.password = Bytes::of(&c.password);
        opts.known_hosts = Bytes::of(&c.known_hosts);
        opts.pinned = Bytes::of(&c.pinned);
        opts.private_key = Bytes::of(&c.private_key);
        opts.port = c.port;
        opts.pool = 1;
        opts.flags = 1 | if c.insecure { 2 } else { 0 };
        opts.probe = 1000;
        opts.keepalive = 1000;
        opts.keepalive_max = 3;
        let mut id = 0;
        let status = unsafe {
            (self.symbol::<unsafe extern "C" fn(u64, *const ConnectOptions, *mut u64) -> i32>(
                b"pmac_connect_submit\0",
            )?)(self.client, &opts, &mut id)
        };
        self.finish(id, status).map(|_| ())
    }
    fn options(timeout: u32) -> Options {
        Options {
            size: size_of::<Options>() as u32,
            abi: ABI,
            timeout,
            flags: 4,
            max_response: 16 * 1024 * 1024,
        }
    }
    pub fn command(&self, text: &str, timeout: u32) -> Result<String, String> {
        let mut id = 0;
        let opts = Self::options(timeout);
        let status = unsafe {
            (self.symbol::<unsafe extern "C" fn(u64,*const u8,usize,*const Options,*mut u64)->i32>(b"pmac_get_response_async\0")?)(self.client,text.as_ptr(),text.len(),&opts,&mut id)
        };
        self.finish(id, status)
    }
    pub fn shell(&self, text: &str, timeout: u32) -> Result<String, String> {
        let opts = Self::options(timeout);
        let mut id = 0;
        let status = unsafe {
            (self.symbol::<unsafe extern "C" fn(
                u64,
                *const u8,
                usize,
                u32,
                *const Options,
                *mut u64,
            ) -> i32>(b"pmac_shell_submit\0")?)(
                self.client,
                text.as_ptr(),
                text.len(),
                0,
                &opts,
                &mut id,
            )
        };
        self.finish(id, status)
    }
    pub fn close(&mut self) -> Result<(), String> {
        if self.client != 0 {
            let status =
                unsafe {
                    (self.symbol::<unsafe extern "C" fn(u64, u32, u32) -> i32>(
                        b"pmac_client_close\0",
                    )?)(self.client, 0, 5000)
                };
            self.check(status, 0)?;
            let status = unsafe {
                (self.symbol::<unsafe extern "C" fn(u64) -> i32>(b"pmac_client_release\0")?)(
                    self.client,
                )
            };
            self.check(status, 0)?;
            self.client = 0;
        }
        if self.runtime != 0 {
            let status =
                unsafe {
                    (self.symbol::<unsafe extern "C" fn(u64, u32) -> i32>(
                        b"pmac_runtime_shutdown\0",
                    )?)(self.runtime, 5000)
                };
            self.check(status, 0)?;
            let status = unsafe {
                (self.symbol::<unsafe extern "C" fn(u64) -> i32>(b"pmac_runtime_release\0")?)(
                    self.runtime,
                )
            };
            self.check(status, 0)?;
            self.runtime = 0;
        }
        Ok(())
    }
}
impl Drop for Sdk {
    fn drop(&mut self) {
        if let Err(e) = self.close() {
            // Keep the module mapped when the runtime could still own active native threads.
            eprintln!("清理失败，保留 DLL 映射直到进程退出: {e}");
            // ManuallyDrop keeps this exact library mapped on cleanup failure.
        } else {
            unsafe {
                std::mem::ManuallyDrop::drop(&mut self.lib);
            }
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn abi_layout() {
        assert_eq!(size_of::<Bytes>(), 16);
        assert_eq!(size_of::<RuntimeConfig>(), 16);
        assert_eq!(size_of::<ClientConfig>(), 24);
        assert_eq!(size_of::<ConnectOptions>(), 192);
        assert_eq!(size_of::<Options>(), 24);
        assert_eq!(size_of::<Info>(), 80);
        assert_eq!(size_of::<ErrorInfo>(), 32);
    }
}
