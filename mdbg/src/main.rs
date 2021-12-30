extern crate cli;

use debugger::MidasCommuncation;
use libc;
use std::ffi::OsString;
mod commands;
mod debugger;
mod software_breakpoint;
mod types;

pub trait CommuncationProtocol {
    fn prepare_message()
}

fn main() -> Result<(), OsString> {
    let args: Vec<_> = std::env::args_os().collect();
    let program_path = args.get(1).ok_or("no binary provided")?;
    let (cli_output_stream, debugger_input_stream) = std::sync::mpsc::channel();
    let (debugger_output_stream, cli_input_stream) = std::sync::mpsc::channel();
    let midas_communications =
        MidasCommuncation::new(debugger_input_stream, debugger_output_stream);
    match wrapper::fork()? {
        wrapper::Fork::Parent(pid) => {
            let mut cli = Box::new(cli::Prompt::new("mdb> ")?);
            let debugger = debugger::Debugger::new(program_path.clone(), pid, midas_communications);
            loop {
                let input = debugger.wait_for_input();
                debugger.handle_command(commands::parse_input(input));
            }
        }
        wrapper::Fork::Child => match wrapper::trace_execution_of(&program_path) {
            Ok(()) => todo!("execution successfully began. Not implemented."),
            Err(err) => return Err(err.into()),
        },
    }
}
