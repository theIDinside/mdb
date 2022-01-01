use super::types::Address;
use nixwrap::Pid;

#[allow(dead_code)]
pub enum BreakpointRequest {
    Address(Address),
    Line { number: usize, file: String },
    Function { name: String, file: Option<String> },
}

pub struct Breakpoint {
    pub address: Address,
    _instruction_binary: isize,
    _pid: Pid,
}

pub struct HWBreakpoint {}

impl Breakpoint {}

impl HWBreakpoint {}
