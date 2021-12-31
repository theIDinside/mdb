use crate::software_breakpoint::BreakpointRequest;
use libc::pid_t;

#[allow(dead_code)]
pub enum Command {
    SetBreakpoint(BreakpointRequest),
    Run(usize),
    ListLines(usize),
    RunAll,
    Step { count: Option<usize> },
    StepIn,
    Next { count: Option<usize> },
    Finish,
    Noop,
    Info,
    Unknown,
}
