use midas::{commands::Command, software_breakpoint::BreakpointRequest};

use crate::parse_hex_string;
#[allow(dead_code)]
pub fn parse_input(input: String) -> Command {
    match &input[..] {
        "info" | "i" => Command::Info,
        _ => Command::Unknown,
    }
}

pub enum ReplCommands {
    Quit,
    Run,
    SetBreakpoint(Result<BreakpointRequest, String>),
    UnknownCommand,
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
        _ => ReplCommands::UnknownCommand,
    }
}
