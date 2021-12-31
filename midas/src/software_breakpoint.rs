use super::types::Address;
use libc::pid_t;

#[allow(dead_code)]
pub enum BreakpointRequest {
    Address(Address),
    Line { number: usize, file: String },
    Function { name: String, file: Option<String> },
}

pub struct Breakpoint {
    pub address: Address,
    instruction_binary: isize,
    pid: pid_t,
}

impl Breakpoint {
    pub fn set_breakpoint(address: usize) -> Result<Breakpoint, String> {}
}
