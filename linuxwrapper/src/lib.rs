use libc::pid_t;
use std::os::unix::prelude::OsStrExt;
pub mod waitstatus;
pub use waitstatus::{Pid, WaitStatus};
pub mod errno;
pub mod ptrace;
pub mod signals;
pub type MidasSysResult<T> = Result<T, String>;
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
mod tests {}
