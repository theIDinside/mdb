use nixwrap::WaitStatus;

use crate::MidasSysResult;

#[cfg(target_os = "linux")]
pub struct Pid(libc::pid_t);

// represents the state operations we can do on the debuggeee
pub trait Target {
    fn process_id(&self) -> Pid;
    fn step(&self, steps: usize);
    fn exec_continue(&self);
    fn kill(&self) -> MidasSysResult<WaitStatus>;
}
