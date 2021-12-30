use crate::software_breakpoint::BreakpointRequest;

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

type CommandParse<T> = Result<T, &'static str>;

pub fn parse_input(input: String) -> Command {
    match &input[..] {
        "info" | "i" => Command::Info,
        _ => Command::Unknown,
    }
}
