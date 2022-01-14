use super::types::Address;
use nixwrap::ptrace;
use nixwrap::{MidasSysResultDynamic, Pid};

// todo(simon): implement hardware breakpoints. a bit more involved and platform-dependent to the MAX(nth) degree.
pub struct HWBreakpoint {}
impl HWBreakpoint {}

#[allow(dead_code)]
pub enum BreakpointRequest {
    Address(Address),
    SourceCodeLocation {
        line: usize,
        column: usize,
        file: String,
    },
    Function {
        name: String,
        file: Option<String>,
    },
}

impl BreakpointRequest {
    pub fn source(line: usize, column: usize, file: String) -> BreakpointRequest {
        BreakpointRequest::SourceCodeLocation { line, column, file }
    }

    pub fn address<A: Into<Address>>(address: A) -> BreakpointRequest {
        BreakpointRequest::Address(address.into())
    }

    pub fn function<S>(name: S, file: Option<S>) -> BreakpointRequest
    where
        S: ToString + Into<String>,
    {
        BreakpointRequest::Function {
            name: name.into(),
            file: file.map(|a| a.to_string()),
        }
    }

    pub fn description(&self) -> &'static str {
        match self {
            BreakpointRequest::Address(..) => "Sets a breakpoint at the provided <address>: <0x123abc>",
            BreakpointRequest::SourceCodeLocation { .. } => "Set a breakpoint in the provided <file and line>: <file:line>",
            BreakpointRequest::Function { .. } => "Sets a breakpoint on all functions with the provided name. Does not discriminate between namespaces or member functions. A::foo and foo is the same. If a file name is provided as a parameter, this will be a predicate.",
        }
    }
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct Breakpoint {
    pub address: Address,
    pub enabled: bool,
    pid: Pid,
    instruction_encoding: i64,
}

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
