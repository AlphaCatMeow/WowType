//! 和操作系统打交道的几处小事：本地时间、家目录和缓存目录、起子进程。
//!
//! Linux、macOS 照原来的写法（`localtime_r`、`$HOME`、`$XDG_CACHE_HOME`）；Windows（TSF 外壳，
//! docs/WINDOWS.md）用 CRT 的 `localtime_s`、`%USERPROFILE%`、`%LOCALAPPDATA%`，起 curl、llama-server
//! 这些控制台程序时不弹黑框。

use std::path::PathBuf;
use std::process::Command;

use crate::datetime::LocalTime;

/// Unix 秒 → 本地时间（按系统时区）。拿不到返回 None。
pub fn local_time(seconds: i64) -> Option<LocalTime> {
    let tm = local_tm(seconds)?;
    Some(LocalTime {
        year: tm.tm_year + 1900,
        month: (tm.tm_mon + 1).max(0) as u32,
        day: tm.tm_mday.max(0) as u32,
        hour: tm.tm_hour.max(0) as u32,
        minute: tm.tm_min.max(0) as u32,
        second: tm.tm_sec.max(0) as u32,
        weekday: tm.tm_wday.max(0) as u32,
    })
}

/// 现在的本地时间。
pub fn local_now() -> Option<LocalTime> {
    let seconds = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .ok()?
        .as_secs();
    local_time(i64::try_from(seconds).ok()?)
}

#[cfg(unix)]
fn local_tm(seconds: i64) -> Option<libc::tm> {
    let time = libc::time_t::try_from(seconds).ok()?;
    let mut tm: libc::tm = unsafe { std::mem::zeroed() };
    // SAFETY: localtime_r 只读 time、只写我们给的 tm
    if unsafe { libc::localtime_r(&time, &mut tm) }.is_null() {
        return None;
    }
    Some(tm)
}

#[cfg(windows)]
fn local_tm(seconds: i64) -> Option<libc::tm> {
    let time = libc::time_t::try_from(seconds).ok()?;
    let mut tm: libc::tm = unsafe { std::mem::zeroed() };
    // SAFETY: localtime_s（CRT 的 _localtime64_s）只读 time、只写我们给的 tm；失败返回非 0
    if unsafe { libc::localtime_s(&mut tm, &time) } != 0 {
        return None;
    }
    Some(tm)
}

#[cfg(not(any(unix, windows)))]
fn local_tm(_seconds: i64) -> Option<libc::tm> {
    None
}

/// 家目录：`$HOME`，Windows 上没设（一般都没设）时用 `%USERPROFILE%`。
pub fn home_dir() -> Option<PathBuf> {
    let from = |name: &str| {
        std::env::var_os(name)
            .filter(|value| !value.is_empty())
            .map(PathBuf::from)
    };
    let home = from("HOME");
    #[cfg(windows)]
    let home = home.or_else(|| from("USERPROFILE"));
    home
}

/// 放可以重建的缓存（词库快照）的目录：`$XDG_CACHE_HOME`，Windows 上是 `%LOCALAPPDATA%`，
/// 再不然 `~/.cache`。调用方在后面接 `wowtype/`。
pub fn cache_dir() -> Option<PathBuf> {
    let from = |name: &str| {
        std::env::var_os(name)
            .filter(|value| !value.is_empty())
            .map(PathBuf::from)
    };
    let dir = from("XDG_CACHE_HOME");
    #[cfg(windows)]
    let dir = dir.or_else(|| from("LOCALAPPDATA"));
    dir.or_else(|| home_dir().map(|home| home.join(".cache")))
}

/// 起控制台程序（curl、llama-server）前调：Windows 上输入法跑在别人的窗口程序里，
/// 不加 CREATE_NO_WINDOW 会弹一个黑框。别的系统什么也不做。
pub fn no_console_window(command: &mut Command) -> &mut Command {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }
    command
}

/// Windows：把子进程放进一个「句柄关了就杀掉里面所有进程」的 Job Object。
///
/// Job 的句柄放在 static 里一直不关，输入法所在的进程（记事本、浏览器……）一退出，系统关掉句柄，
/// llama-server 跟着退出，和 Linux 上的 PR_SET_PDEATHSIG 一个作用。建不了、放不进去就算了
/// （照以前那样起，不为这个翻译不了）。
#[cfg(windows)]
pub fn tie_to_this_process(child: &std::process::Child) {
    use std::os::windows::io::AsRawHandle;
    use std::sync::OnceLock;

    type Handle = isize;
    const JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS: i32 = 9;
    const JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: u32 = 0x2000;

    #[repr(C)]
    #[derive(Default)]
    struct BasicLimit {
        per_process_user_time_limit: i64,
        per_job_user_time_limit: i64,
        limit_flags: u32,
        minimum_working_set_size: usize,
        maximum_working_set_size: usize,
        active_process_limit: u32,
        affinity: usize,
        priority_class: u32,
        scheduling_class: u32,
    }
    #[repr(C)]
    #[derive(Default)]
    struct IoCounters {
        counts: [u64; 6],
    }
    #[repr(C)]
    #[derive(Default)]
    struct ExtendedLimit {
        basic: BasicLimit,
        io: IoCounters,
        process_memory_limit: usize,
        job_memory_limit: usize,
        peak_process_memory_used: usize,
        peak_job_memory_used: usize,
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn CreateJobObjectW(attributes: *const core::ffi::c_void, name: *const u16) -> Handle;
        fn SetInformationJobObject(
            job: Handle,
            class: i32,
            info: *const core::ffi::c_void,
            length: u32,
        ) -> i32;
        fn AssignProcessToJobObject(job: Handle, process: Handle) -> i32;
    }

    static JOB: OnceLock<Handle> = OnceLock::new();
    let job = *JOB.get_or_init(|| {
        // SAFETY: 参数都是空指针或者我们自己的结构体，长度和结构体一致
        unsafe {
            let job = CreateJobObjectW(std::ptr::null(), std::ptr::null());
            if job == 0 {
                return 0;
            }
            let mut info = ExtendedLimit::default();
            info.basic.limit_flags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(
                job,
                JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS,
                &info as *const ExtendedLimit as *const core::ffi::c_void,
                std::mem::size_of::<ExtendedLimit>() as u32,
            );
            job
        }
    });
    if job != 0 {
        // SAFETY: 子进程的句柄在 child 活着时有效
        unsafe {
            AssignProcessToJobObject(job, child.as_raw_handle() as Handle);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn local_time_is_sane() {
        // 2024-06-15 12:00:00 UTC：不管哪个时区都还是 6 月 14 到 16 号
        let t = local_time(1_718_452_800).expect("本地时间");
        assert_eq!((t.year, t.month), (2024, 6));
        assert!((14..=16).contains(&t.day), "{}", t.day);
        assert!(t.hour < 24 && t.minute < 60 && t.weekday < 7);
        assert!(local_now().is_some());
    }

    #[test]
    fn dirs_exist_somewhere() {
        // CI 的 Linux、macOS 有 HOME，Windows 有 USERPROFILE、LOCALAPPDATA
        assert!(home_dir().is_some());
        assert!(cache_dir().is_some());
    }
}
