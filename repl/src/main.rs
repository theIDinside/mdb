extern crate cli;
extern crate linuxwrapper as nixwrap;
extern crate midas;

use std::ffi::OsString;
mod commands;

fn main() -> Result<(), String> {
    let args: Vec<String> = std::env::args().collect();
    let program_path = args
        .get(1)
        .ok_or(|| "You did not provide a binary".into())?;
    let mut p = cli::Prompt::new("midas> ")?;
    loop {
        let input = p.read_input();
        let parts: Vec<String> = input.split(" ").collect();
        let cmd = parts.get(0).unwrap_or("");
        match &cmd[..] {
            "q" | "quit" => {
                p.display_output("quitting");
                return Ok(());
            }
            "r" | "run" => {
                let object = midas::elf::load_object(std::path::Path::new(program_path))?;
                let elf = midas::elf::ParsedELF::parse_elf(&obj).map_err(|e| format!("{}", e.description()))?;
                let (target_, waitstatus) = midas::target::linux::LinuxTarget::launch(
                    &mut target::make_command(program_path, vec!["exited"]).unwrap(),
                )
                .unwrap();
            }
            "b" | "breakpoint" => {
                let params = &parts[1..];
                if params.len() < 1 {
                    p.display_output(
                        "breakpoint command requires parameters: <address | function | symbol | source location>",
                    );
                } else {
                    if let Ok(addr) = &params[0].parse::<usize>() {
                    } else {
                        p.display_output("Invalid address");
                    }
                }
            }
            _ => {
                p.display_output(&format!("You wrote: {}", input));
            }
        }
    }
}
