use nixwrap::{waitpid, MidasSysResultDynamic, Pid, WaitStatus};
use std::{
    collections::{HashMap, HashSet},
    os::unix::prelude::CommandExt,
};

use crate::{software_breakpoint::Breakpoint, types::Address, MidasSysResult};

pub struct LinuxTarget {
    _binary: String,
    pid: Pid,
    _software_breakpoints: HashMap<Address, Vec<Breakpoint>>,
}

impl super::Target for LinuxTarget {
    fn launch(command: &mut std::process::Command) -> MidasSysResultDynamic<(Box<dyn super::Target>, WaitStatus)> {
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
                        nixwrap::ptrace::trace_me()
                            .map_err(|err_string| std::io::Error::new(std::io::ErrorKind::Other, err_string))?;
                        Ok(())
                    }
                });
                let child = command
                    .spawn()
                    .map_err(|err| format!("Spawning child failed: {}", err))?;
                let pid = Pid(child.id() as _);
                let status = waitpid(*pid, 0)?;
                let target = Box::new(LinuxTarget {
                    _binary: path.to_str().unwrap().to_string(),
                    pid: pid,
                    _software_breakpoints: HashMap::new(),
                });
                Ok((target, status))
            }
        }
    }

    fn process_id(&self) -> Pid {
        self.pid
    }

    fn step(&mut self, _steps: usize) {
        todo!()
    }

    fn continue_execution(&mut self) -> nixwrap::MidasSysResultDynamic<nixwrap::WaitStatus> {
        let pc = nixwrap::ptrace::get_regs(self.process_id())
            .pc()
            .saturating_sub(1);
        let hit_breakpoint = match self._software_breakpoints.get_mut(&Address(pc as usize)) {
            Some(bp_set) => {
                let mut disabled = false;
                for bp in bp_set.iter_mut() {
                    if bp.enabled && !disabled {
                        bp.disable();
                        disabled = true;
                    }
                    bp.set_is_enabled(false);
                }
                true
            }
            _ => false,
        };
        if hit_breakpoint {
            println!(
                "Breakpoint was hit prior to continue; 0x{:X}, continuing",
                pc
            );
            nixwrap::ptrace::set_pc(self.process_id(), pc as usize);
        }
        nixwrap::continue_execution(*self.pid).unwrap();
        let opts = 0;
        nixwrap::waitpid(*self.pid, opts)
    }

    fn kill(&mut self) -> nixwrap::MidasSysResultDynamic<nixwrap::WaitStatus> {
        todo!()
    }

    fn read_memory(&self, _address: usize, _bytes: usize) -> Vec<u8> {
        todo!()
    }

    fn kill_on_tracer_exit(&mut self) -> nixwrap::MidasSysResultDynamic<()> {
        nixwrap::ptrace::kill_on_midas_exit(self.process_id())
    }

    fn set_breakpoint(&mut self, bp: crate::software_breakpoint::BreakpointRequest) -> MidasSysResultDynamic<()> {
        match bp {
            crate::software_breakpoint::BreakpointRequest::Address(Address(addr)) => {
                let bp = super::super::software_breakpoint::Breakpoint::set_enabled(self.pid, addr)?;
                let key = Address(addr);
                if let Some(set) = self._software_breakpoints.get_mut(&key) {
                    set.push(bp);
                } else {
                    self._software_breakpoints.insert(key, vec![bp]);
                }
                Ok(())
            }
            crate::software_breakpoint::BreakpointRequest::Line { number, file } => todo!(),
            crate::software_breakpoint::BreakpointRequest::Function { name, file } => todo!(),
        }
    }
}
