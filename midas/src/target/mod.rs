use libc::{ptrace, PTRACE_SETOPTIONS};
use nixwrap::{get_errno_msg, unwrap_err_err, Pid, WaitStatus};
pub mod linux;

use crate::MidasSysResult;

// represents the state operations we can do on the debuggeee
pub trait Target {
    fn process_id(&self) -> Pid;
    fn step(&self, steps: usize);
    fn exec_continue(&self);
    fn kill(&self) -> MidasSysResult<WaitStatus>;
    fn read_memory(&self, address: usize, bytes: usize) -> Vec<u8>;
    fn kill_on_tracer_exit(&self) -> MidasSysResult<()> {
        unsafe {
            if ptrace(
                PTRACE_SETOPTIONS,
                *self.process_id(),
                std::ptr::null() as *const libc::c_void,
                std::ptr::null() as *const libc::c_void,
            ) == -1
            {
                Err(unwrap_err_err(get_errno_msg()))
            } else {
                Ok(())
            }
        }
    }
}
