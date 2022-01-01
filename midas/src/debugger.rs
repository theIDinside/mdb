use super::commands::Command;
use crate::{software_breakpoint::Breakpoint, types::Address};
use nixwrap::MidasSysResult;
use nixwrap::{Pid, WaitStatus};
use std::{
    collections::{HashMap, HashSet},
    ops::Deref,
};

pub struct Debugger {
    binary: String,
    pid: Pid,
    software_breakpoints: HashMap<Address, HashSet<Breakpoint>>,
}

impl Debugger {
    pub fn new(binary: String, pid: Pid) -> Debugger {
        Debugger {
            binary,
            pid,
            software_breakpoints: HashMap::new(),
        }
    }

    pub fn continue_execution(&mut self) -> MidasSysResult<WaitStatus> {
        nixwrap::continue_execution(*self.pid).unwrap();
        let opts = 0;
        nixwrap::waitpid(*self.pid, opts)
    }

    // Public API for repl, server, etc to communicate with. *Everything* goes through here.
    pub fn handle_command(&mut self, cmd: Command) {
        todo!("handle of request commands not implemented")
    }
}
