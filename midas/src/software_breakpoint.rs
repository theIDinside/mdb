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
    instruction_binary: isize,
    pid: Pid,
}

pub struct HWBreakpoint {}

impl Breakpoint {}

impl HWBreakpoint {}
