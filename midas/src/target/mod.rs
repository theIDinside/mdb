use nixwrap::{Pid, WaitStatus};
pub mod linux;

use nixwrap::MidasSysResult;

// represents the state operations we can do on the debuggeee
pub trait Target {
    type OSTarget;
    fn launch(
        path: &std::path::Path,
    ) -> MidasSysResult<(Box<<Self as Target>::OSTarget>, WaitStatus)>;
    fn process_id(&self) -> Pid;
    fn step(&self, steps: usize);
    fn continue_execution(&self) -> MidasSysResult<WaitStatus>;
    fn kill(&self) -> MidasSysResult<WaitStatus>;
    fn read_memory(&self, address: usize, bytes: usize) -> Vec<u8>;
    fn kill_on_tracer_exit(&self) -> MidasSysResult<()>;
}
