use crate::types::Address;
use libc::pid_t;
use std::collections::{HashMap, HashSet};

pub struct Debugger {
    binary: String,
    pid: pid_t,
    software_breakpoints: HashMap<Address, HashSet<Breakpoint>>,
}

impl Debugger {
    pub fn new(binary: String, pid: pid_t) -> Debugger {
        Debugger {
            binary,
            pid,
            software_breakpoints: HashMap::new(),
        }
    }

    pub fn continue_execution(&mut self) {
        nixwrap::continue_execution(self.pid).unwrap();
        let opts = 0;
        let wait_status = nixwrap::waitpid(self.pid, opts).unwrap();
    }

    pub fn run(&mut self) {
        nixwrap::continue_execution(self.pid).unwrap();
        let opts = 0;
        let wait_status = nixwrap::waitpid(self.pid, opts).unwrap();
    }
}
