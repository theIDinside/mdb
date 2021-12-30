use std::{ffi::OsStr, os::unix::prelude::OsStrExt};

use libc::pid_t;

pub enum Fork {
    Parent(pid_t),
    Child,
}

pub fn get_errno_msg() -> Result<String, String> {
    unsafe {
        let ptr = libc::__errno_location();
        if ptr.is_null() {
            return Err("Could not retrieve errno location".into());
        }
        let err_msg = libc::strerror(*ptr);
        if err_msg.is_null() {
            return Err(format!(
                "Could not retrieve errno message for errno: {}",
                *ptr
            ));
        }
        let err = std::ffi::CString::from_raw(err_msg);
        if err.as_bytes().is_empty() {
            return Err("No errno message found".into());
        }
        err.to_str()
            .map_err(|e| format!("{:?}", e))
            .map(|v| v.to_string())
    }
}

pub fn fork() -> Result<Fork, String> {
    unsafe {
        let pid = libc::fork();
        if pid == -1 {
            let err = get_errno_msg();
            match err {
                Ok(err_msg) => Err(format!("Fork failed: [{}]", err_msg)),
                Err(err_msg) => Err(format!(
                    "Fork failed; Retrieving err message also failed: [{}]",
                    err_msg
                )),
            }
        } else {
            if pid == 0 {
                Ok(Fork::Child)
            } else {
                Ok(Fork::Parent(pid))
            }
        }
    }
}

fn unwrap_err_err(err: Result<String, String>) -> String {
    match err {
        Ok(s) => format!("System reported error: {}", s),
        Err(s) => format!("Failed to get system reported error: {}", s),
    }
}

pub fn trace_execution_of(binary_path: &OsStr) -> Result<(), String> {
    use libc::{execl, ptrace, PTRACE_TRACEME};
    let p = std::path::Path::new(binary_path);
    if !p.exists() {
        Err(format!("{:?} doesn't exist", binary_path))
    } else {
        unsafe {
            if ptrace(PTRACE_TRACEME) == -1 {
                return Err(unwrap_err_err(get_errno_msg()));
            } else {
                if execl(p.as_os_str().as_bytes().as_ptr() as _, std::ptr::null()) == -1 {
                    return Err(unwrap_err_err(get_errno_msg()));
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        let result = 2 + 2;
        assert_eq!(result, 4);
    }
}
