use std::ops::Deref;

use crate::signals::Signal;
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct Pid(pub libc::pid_t);

impl Deref for Pid {
    type Target = libc::pid_t;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub enum WaitStatus {
    /** WIFCONTINUED */
    Continued(Pid),
    /** WIFEXITED */
    ExitedNormally(Pid, i32),
    /** WIFSTOPPED */
    Stopped(Pid, Signal),
    /** WIFSIGNALLED */
    Killed(Pid, Signal),
    /** WCOREDUMP */
    CoreDumped(Pid),
}

impl WaitStatus {
    pub fn from_raw(pid: Pid, wait_status_raw_value: i32) -> Result<Self, String> {
        use libc::{WCOREDUMP, WEXITSTATUS, WIFCONTINUED, WIFEXITED, WIFSIGNALED, WIFSTOPPED};
        if WIFCONTINUED(wait_status_raw_value) {
            Ok(WaitStatus::Continued(pid))
        } else if WIFEXITED(wait_status_raw_value) {
            Ok(WaitStatus::ExitedNormally(
                pid,
                WEXITSTATUS(wait_status_raw_value),
            ))
        } else if WIFSTOPPED(wait_status_raw_value) {
            Ok(WaitStatus::Stopped(
                pid,
                Signal::from_raw(libc::WSTOPSIG(wait_status_raw_value)).unwrap(),
            ))
        } else if WIFSIGNALED(wait_status_raw_value) {
            let signal = Signal::from_raw(libc::WTERMSIG(wait_status_raw_value)).unwrap();
            if WCOREDUMP(wait_status_raw_value) {
                Ok(WaitStatus::CoreDumped(pid))
            } else {
                Ok(WaitStatus::Killed(pid, signal))
            }
        } else {
            Err("Failed to get wait status".into())
        }
    }
}
