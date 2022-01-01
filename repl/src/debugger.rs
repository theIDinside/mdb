use midas::software_breakpoint::Breakpoint;
use midas::types::Address;

use std::collections::{HashMap, HashSet};
use std::ffi::OsString;
use std::sync::mpsc;
#[allow(unused)]
pub struct LinuxTarget {
    program_name: OsString,
    pid: libc::pid_t,
}

pub struct MidasCommuncation {
    _input_stream: mpsc::Receiver<String>,
    _output_stream: mpsc::Sender<String>,
}

impl MidasCommuncation {
    #[allow(unused)]
    pub fn new(
        input_stream: mpsc::Receiver<String>,
        output_stream: mpsc::Sender<String>,
    ) -> MidasCommuncation {
        MidasCommuncation {
            _input_stream: input_stream,
            _output_stream: output_stream,
        }
    }
}

impl LinuxTarget {
    #[allow(unused)]
    pub fn new(program_name: OsString, pid: libc::pid_t) -> LinuxTarget {
        LinuxTarget { program_name, pid }
    }
}
#[allow(unused)]
pub struct Debugger {
    target: LinuxTarget,
    software_breakpoints: HashMap<Address, HashSet<Breakpoint>>,
    communication: MidasCommuncation,
}

impl Debugger {
    #[allow(unused)]
    pub fn new(
        program_name: OsString,
        pid: libc::pid_t,
        communication: MidasCommuncation,
    ) -> Debugger {
        Debugger {
            target: LinuxTarget::new(program_name, pid),
            software_breakpoints: HashMap::new(),
            communication,
        }
    }
    #[allow(unused)]
    pub(crate) fn handle_command(&self, command: midas::commands::Command) {}
    #[allow(unused)]
    pub(crate) fn wait_for_input(&self) -> String {
        todo!()
    }
}
