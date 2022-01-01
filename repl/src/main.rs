extern crate cli;
extern crate linuxwrapper as nixwrap;

use std::ffi::OsString;
mod commands;
mod debugger;

fn main() -> Result<(), OsString> {
    Ok(())
}
