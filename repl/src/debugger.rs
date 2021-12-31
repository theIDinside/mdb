use midas::commands::Command;
use midas::software_breakpoint::Breakpoint;
use midas::types::Address;

use std::collections::{HashMap, HashSet};
use std::ffi::OsString;
use std::sync::mpsc;

pub struct LinuxTarget {
    program_name: OsString,
    pid: libc::pid_t,
}

pub struct MidasCommuncation {
    input_stream: mpsc::Receiver<String>,
    output_stream: mpsc::Sender<String>,
}

impl MidasCommuncation {
    pub fn new(
        input_stream: mpsc::Receiver<String>,
        output_stream: mpsc::Sender<String>,
    ) -> MidasCommuncation {
        MidasCommuncation {
            input_stream,
            output_stream,
        }
    }
}

impl LinuxTarget {
    pub fn new(program_name: OsString, pid: libc::pid_t) -> LinuxTarget {
        LinuxTarget { program_name, pid }
    }
}

pub struct Debugger {
    target: LinuxTarget,
    software_breakpoints: HashMap<Address, HashSet<Breakpoint>>,
    communication: MidasCommuncation,
}

impl Debugger {
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

    pub(crate) fn handle_command(&self, command: midas::commands::Command) {
        match command {
            Command::SetBreakpoint(bp_req) => todo!(),
            Command::Run(thread_id) => todo!(),
            Command::ListLines(line_count) => todo!(),
            Command::RunAll => todo!(),
            Command::Step { count } => todo!(),
            Command::StepIn => todo!(),
            Command::Next { count } => todo!(),
            Command::Finish => todo!(),
            Command::Noop => todo!(),
            Command::Info => todo!(),
            Command::Unknown => todo!(),
        }
    }

    pub(crate) fn wait_for_input(&self) -> String {
        todo!()
    }
}
