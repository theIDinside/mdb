use libc::pid_t;
use std::os::unix::prelude::OsStrExt;
pub mod waitstatus;
pub use waitstatus::{Pid, WaitStatus};
mod errno;
pub mod ptrace;

pub enum Fork {
    Parent(pid_t),
    Child,
}

pub fn fork() -> Result<Fork, String> {
    unsafe {
        let pid = libc::fork();
        if pid == -1 {
            let err = errno::get_errno_msg();
            Err(format!("Fork failed: [{}]", err))
        } else {
            if pid == 0 {
                Ok(Fork::Child)
            } else {
                Ok(Fork::Parent(pid))
            }
        }
    }
}
/** Takes a Result<String, String> from get_errno_msg() and turns it into a String
 *
 */
pub fn unwrap_err_err(err: Result<String, String>) -> String {
    match err {
        Ok(s) => format!("System reported error: {}", s),
        Err(s) => format!("Failed to get system reported error: {}", s),
    }
}

pub fn begin_trace_target(target_binary_path: &std::path::Path) -> Result<(), String> {
    use libc::{execl, ptrace, PTRACE_TRACEME};
    let p = std::path::Path::new(target_binary_path);
    if !p.exists() {
        Err(format!("{:?} doesn't exist", target_binary_path))
    } else {
        unsafe {
            if ptrace(
                PTRACE_TRACEME,
                0,
                std::ptr::null() as *const libc::c_void,
                std::ptr::null() as *const libc::c_void,
            ) == -1
            {
                return Err(errno::get_errno_msg());
            } else {
                if execl(p.as_os_str().as_bytes().as_ptr() as _, std::ptr::null()) == -1 {
                    return Err(errno::get_errno_msg());
                }
            }
        }
        Ok(())
    }
}

pub fn continue_execution(pid: pid_t) -> Result<(), String> {
    use libc::{ptrace, PTRACE_CONT};
    unsafe {
        if ptrace(
            PTRACE_CONT,
            pid,
            std::ptr::null() as *const libc::c_void,
            std::ptr::null() as *const libc::c_void,
        ) == -1
        {
            return Err(errno::get_errno_msg());
        }
    }
    Ok(())
}

pub fn waitpid(pid: pid_t, options: i32) -> Result<WaitStatus, String> {
    let mut v: i32 = 0;
    unsafe {
        if libc::waitpid(pid, &mut v, options) == -1 {
            return Err(errno::get_errno_msg());
        }
    }
    WaitStatus::from_raw(Pid(pid), v)
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        let result = 2 + 2;
        assert_eq!(result, 4);
    }
}
