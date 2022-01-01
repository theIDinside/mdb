extern crate cli;
extern crate linuxwrapper as nixwrap;

use debugger::MidasCommuncation;
use std::ffi::OsString;
mod commands;
mod debugger;

fn main() -> Result<(), OsString> {
    let args: Vec<_> = std::env::args_os().collect();
    let program_path = args.get(1).ok_or("no binary provided")?;
    let (cli_output_stream, debugger_input_stream) = std::sync::mpsc::channel();
    let (debugger_output_stream, cli_input_stream) = std::sync::mpsc::channel();
    let midas_communications =
        MidasCommuncation::new(debugger_input_stream, debugger_output_stream);
    Ok(())
}
