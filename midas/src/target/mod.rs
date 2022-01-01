use std::convert::From;

use nixwrap::{Pid, WaitStatus};
pub mod linux;

use nixwrap::MidasSysResult;

// represents the state operations we can do on the debuggeee
pub trait Target {
    type OSTarget;
    fn launch(
        command: &mut std::process::Command,
    ) -> MidasSysResult<(Box<<Self as Target>::OSTarget>, WaitStatus)>;
    fn process_id(&self) -> Pid;
    fn step(&self, steps: usize);
    fn continue_execution(&self) -> MidasSysResult<WaitStatus>;
    fn kill(&self) -> MidasSysResult<WaitStatus>;
    fn read_memory(&self, address: usize, bytes: usize) -> Vec<u8>;
    fn kill_on_tracer_exit(&self) -> MidasSysResult<()>;
}

pub fn make_command(program_path: &str, args: Vec<&str>) -> MidasSysResult<std::process::Command> {
    let program = std::path::Path::new(program_path);
    if !program.exists() {
        Err(format!("{} doesn't exist", program.display()))
    } else {
        let mut command = std::process::Command::new(program);
        command.args(args.iter());
        Ok(command)
    }
}
