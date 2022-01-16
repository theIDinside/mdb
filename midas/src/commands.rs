use crate::{software_breakpoint::BreakpointRequest, step::StepRequest};

pub enum InfoCommand {
    Registers,
    ListBreakpoints,
    GlobalVariables,
}

impl InfoCommand {
    pub fn description(&self) -> &'static str {
        match self {
            InfoCommand::Registers => "Display contents of the registers",
            InfoCommand::ListBreakpoints => "List information about breakpoints",
            InfoCommand::GlobalVariables => "List all global and static variables",
        }
    }
}

#[allow(dead_code)]
pub enum Command {
    SetBreakpoint(BreakpointRequest),
    Run(usize),
    ListLines(usize),
    RunAll,
    Step(StepRequest),
    Finish,
    FinishAll,
    Noop,
    Info(InfoCommand),
    Unknown,
}

impl Command {
    pub fn description(self) -> &'static str {
        match self {
            Command::SetBreakpoint(bp_req) => bp_req.description(),
            Command::Run(..) => "Continue the currently selected thread of the tracee",
            Command::ListLines(..) => "List <N> source lines around current program counter",
            Command::RunAll => "Continue all threads",
            Command::Step(step_req) => step_req.description(),
            Command::Finish => "Continue until this function exits",
            Command::FinishAll => "Continue all threads until they exit the routine they're inside",
            Command::Noop => todo!(),
            Command::Unknown => "Unknown command",
            Command::Info(info_command) => info_command.description(),
        }
    }
}
