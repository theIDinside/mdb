use super::commands::Command;
use crate::{software_breakpoint::Breakpoint, types::Address};
use nixwrap::MidasSysResult;
use nixwrap::{Pid, WaitStatus};
use std::collections::{HashMap, HashSet};

pub struct Debugger {
    _binary: String,
    pid: Pid,
    _software_breakpoints: HashMap<Address, HashSet<Breakpoint>>,
}

impl Debugger {
    pub fn new(binary: String, pid: Pid) -> Debugger {
        Debugger {
            _binary: binary,
            pid,
            _software_breakpoints: HashMap::new(),
        }
    }

    pub fn continue_execution(&mut self) -> MidasSysResult<WaitStatus> {
        nixwrap::continue_execution(*self.pid).unwrap();
        let opts = 0;
        nixwrap::waitpid(*self.pid, opts)
    }

    // Public API for repl, server, etc to communicate with. *Everything* goes through here.
    pub fn handle_command(&mut self, _cmd: Command) {
        todo!("handle of request commands not implemented")
    }
}
