use super::types::Address;
use nixwrap::ptrace;
use nixwrap::{MidasSysResultDynamic, Pid};

#[allow(dead_code)]
pub enum BreakpointRequest {
    Address(Address),
    Line { number: usize, file: String },
    Function { name: String, file: Option<String> },
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
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
        Ok(Breakpoint::new(
            Address(addr),
            enabled,
            pid,
            instruction & 0xff,
        ))
    }

    pub fn disable(&mut self) {
        if self.enabled {
            let instruction = nixwrap::ptrace::peek_data(self.pid, self.address.value()).unwrap();
            let restored = (instruction & !0xff) | self.instruction_encoding;
            ptrace::poke_data(self.pid, self.address.value(), restored).unwrap();
        }
        self.enabled = false;
    }

    // for when we might have a set of breakpoints, we don't want to keep poke_data'ing if we disable them all
    pub fn set_is_enabled(&mut self, value: bool) {
        self.enabled = value;
    }
}

impl HWBreakpoint {}
