use super::types::Address;
use nixwrap::ptrace;
use nixwrap::{MidasSysResultDynamic, Pid};

#[allow(dead_code)]
pub enum BreakpointRequest {
    Address(Address),
    Line { number: usize, file: String },
    Function { name: String, file: Option<String> },
}

pub struct Breakpoint {
    pub address: Address,
    pub enabled: bool,
    pid: Pid,
    instruction_encoding: i64,
}

pub struct HWBreakpoint {}

impl Breakpoint {
    fn new(address: Address, enabled: bool, pid: Pid, instruction_encoding: i64) -> Breakpoint {
        Breakpoint {
            address,
            enabled,
            pid,
            instruction_encoding,
        }
    }
    pub fn set_enabled(pid: Pid, addr: usize) -> MidasSysResultDynamic<Breakpoint> {
        Breakpoint::set(pid, addr, true)
    }
    pub fn set(pid: Pid, addr: usize, enabled: bool) -> MidasSysResultDynamic<Breakpoint> {
        let instruction = nixwrap::ptrace::peek_data(pid, addr)?;
        // call down the old gods upon you
        let interrupt_3 = 0xcc;
        if enabled {
            let swap_in = (instruction & !0xff) | interrupt_3;
            ptrace::poke_data(pid, addr, swap_in)?;
        }
        Ok(Breakpoint::new(Address(addr), enabled, pid, instruction))
    }

    pub fn disable(&mut self) {
        if self.enabled {
            ptrace::poke_data(self.pid, self.address.value(), self.instruction_encoding).unwrap();
        }
        self.enabled = false;
    }
}

impl HWBreakpoint {}
