use crate::parse_hex_string;
use midas::{commands::Command, software_breakpoint::BreakpointRequest};

pub enum ReplCommands {
    Quit,
    Run,
    SetBreakpoint(Result<BreakpointRequest, String>),
    // step machine instructions
    Step(usize),
    // step source line
    Next(usize),
    List(usize),
    UnknownCommand,
    Help(Option<Command>),
}

pub fn parse_user_input(input: &String) -> ReplCommands {
    let parts: Vec<String> = input.split(" ").map(|item| item.to_owned()).collect();
    let cmd = parts.get(0).map(|i| i.clone()).unwrap_or("".to_owned());
    match &cmd[..] {
        "q" | "quit" => {
            return ReplCommands::Quit;
        }
        "r" | "run" => ReplCommands::Run,
        "b" | "breakpoint" => {
            let params = &parts[1..];
            if params.len() < 1 {
                return ReplCommands::SetBreakpoint(Err(
                    "breakpoint command requires parameters: <address | function | symbol | source location>"
                        .to_string(),
                ));
            } else {
                let res = parse_hex_string(&params[0]);
                if let Ok(addr) = res {
                    return ReplCommands::SetBreakpoint(Ok(BreakpointRequest::Address(addr.into())));
                } else {
                    if params[0].contains(":") {
                        // we want to set a breakpoint at a source location. run the line program
                        let split: Vec<&str> = params[0].split(":").collect();
                        let file = split.get(0);
                        let line = split.get(1).and_then(|l| l.parse().ok());
                        if let Some(l) = line {
                            return ReplCommands::SetBreakpoint(Ok(BreakpointRequest::SourceCodeLocation {
                                line: l,
                                column: 0,
                                file: file.unwrap_or(&"").to_string(),
                            }));
                        } else {
                            return ReplCommands::SetBreakpoint(Err("breakpoint command requires parameters: <address | function | symbol | source location>".to_string()));
                        }
                    } else {
                        return ReplCommands::SetBreakpoint(Ok(BreakpointRequest::Function {
                            name: params[0].to_string(),
                            file: None,
                        }));
                    }
                }
            }
        }
        "h" | "help" => {
            if parts.len() > 1 {
                ReplCommands::Help(None)
            } else {
                ReplCommands::Help(None)
            }
        }
        "l" | "list" => {
            if parts.len() > 1 {
                let line_count = &parts[1].parse().unwrap_or(10usize);
                ReplCommands::List(*line_count)
            } else {
                ReplCommands::List(10)
            }
        }
        _ => ReplCommands::UnknownCommand,
    }
}

pub fn prepare_help_output() -> cli::prompt::FormattedBuffer {
    let mut fmt = cli::prompt::FormattedBuffer::with_capacity(350);
    let cmd_fmt = cli::prompt::Format::new().style(cli::prompt::Style::Bold);
    fmt.add_formatted("--- Commands --- \n\r", cmd_fmt);
    fmt.add_formatted("help | h", cmd_fmt);
    fmt.add_unformatted(" --- show this help message\n\r");
    fmt.add_formatted("breakpoint | b", cmd_fmt);
    fmt.add_unformatted(" --- set a breakpoint\n\r");
    fmt.add_formatted("run | r", cmd_fmt);
    fmt.add_unformatted(" --- Continue / start the inferior or tracee process\n\r");
    fmt
}
