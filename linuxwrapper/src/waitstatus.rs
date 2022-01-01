use std::ops::Deref;
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct Pid(pub libc::pid_t);

impl Deref for Pid {
    type Target = libc::pid_t;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct Signal(pub i32);

impl Signal {
    pub fn from(num: i32) -> Self {
        Self(num)
    }
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub enum WaitStatus {
    /** WIFCONTINUED */
    Continued(Pid),
    /** WIFEXITED */
    ExitedNormally(Pid, i32),
    /** WIFSTOPPED */
    Stopped(Pid),
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
            Ok(WaitStatus::Stopped(pid))
        } else if WIFSIGNALED(wait_status_raw_value) {
            let signal = Signal::from(libc::WTERMSIG(wait_status_raw_value));
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
