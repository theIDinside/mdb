use nixwrap::{waitpid, MidasSysResult, Pid, WaitStatus};
use std::{
    collections::{HashMap, HashSet},
    os::unix::prelude::CommandExt,
};

use crate::{software_breakpoint::Breakpoint, types::Address};

pub struct LinuxTarget {
    binary: String,
    pid: Pid,
    software_breakpoints: HashMap<Address, HashSet<Breakpoint>>,
}

impl super::Target for LinuxTarget {
    type OSTarget = LinuxTarget;
    fn launch(
        command: &mut std::process::Command,
    ) -> MidasSysResult<(Box<Self::OSTarget>, WaitStatus)> {
        let pathstr = command.get_program().to_owned();
        let path = std::path::Path::new(&pathstr);
        if !path.exists() {
            Err(format!("binary {} could not be found", path.display()))
        } else {
            unsafe {
                // this closure executes in the forked child code. So in a "regular" old fork situation
                // we would check pid if == 0 or something similar, and then handle accordingly. This closure always execs in the child.
                command.pre_exec(|| {
                    #[cfg(target_os = "linux")]
                    {
                        if libc::personality(libc::ADDR_NO_RANDOMIZE as _) == -1 {
                            panic!("Setting no randomized virtual memory failed");
                        }
                        nixwrap::ptrace::trace_me().map_err(|err_string| {
                            std::io::Error::new(std::io::ErrorKind::Other, err_string)
                        })?;
                        Ok(())
                    }
                });
                let child = command
                    .spawn()
                    .map_err(|err| format!("Spawning child failed: {}", err))?;
                let pid = Pid(child.id() as _);
                let status = waitpid(*pid, 0)?;
                let target = Box::new(LinuxTarget {
                    binary: path.to_str().unwrap().to_string(),
                    pid: pid,
                    software_breakpoints: HashMap::new(),
                });
                Ok((target, status))
            }
        }
    }

    fn process_id(&self) -> Pid {
        self.pid
    }

    fn step(&self, steps: usize) {
        todo!()
    }

    fn continue_execution(&self) -> nixwrap::MidasSysResult<nixwrap::WaitStatus> {
        nixwrap::continue_execution(*self.pid).unwrap();
        let opts = 0;
        nixwrap::waitpid(*self.pid, opts)
    }

    fn kill(&self) -> nixwrap::MidasSysResult<nixwrap::WaitStatus> {
        todo!()
    }

    fn read_memory(&self, address: usize, bytes: usize) -> Vec<u8> {
        todo!()
    }

    fn kill_on_tracer_exit(&self) -> nixwrap::MidasSysResult<()> {
        nixwrap::ptrace::kill_on_midas_exit(self.process_id())
    }
}
