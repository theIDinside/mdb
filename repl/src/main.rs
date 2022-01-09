extern crate cli;
extern crate linuxwrapper as nixwrap;
extern crate midas;
use midas::{
    target::{self, Target},
    types::Address,
};
mod commands;

pub fn parse_hex_string(s: &str) -> Result<usize, &str> {
    let mut value = 0;
    let mut multiplier = 1;
    for c in s.to_uppercase().chars().rev() {
        value += match c {
            '0' => 0 * multiplier,
            '1' => 1 * multiplier,
            '2' => 2 * multiplier,
            '3' => 3 * multiplier,
            '4' => 4 * multiplier,
            '5' => 5 * multiplier,
            '6' => 6 * multiplier,
            '7' => 7 * multiplier,
            '8' => 8 * multiplier,
            '9' => 9 * multiplier,
            'A' => 10 * multiplier,
            'B' => 11 * multiplier,
            'C' => 12 * multiplier,
            'D' => 13 * multiplier,
            'E' => 14 * multiplier,
            'F' => 15 * multiplier,
            _ => return Err("hex parse failed"),
        };
        multiplier *= 16;
    }
    Ok(value)
}

fn main() -> Result<(), String> {
    let args: Vec<String> = std::env::args().collect();
    let program_path = args
        .get(1)
        .ok_or("You did not provide a binary".to_owned())?;
    let mut p = cli::Prompt::new("midas> ")?;
    let object = std::rc::Rc::new(midas::elf::load_object(std::path::Path::new(program_path))?);
    let elf = midas::elf::ParsedELF::parse_elf(&object).map_err(|e| format!("{}", e.description()))?;
    let (mut target_, waitstatus) =
        midas::target::linux::LinuxTarget::launch(&mut target::make_command(program_path, vec!["exited"]).unwrap())
            .unwrap();
    println!("spawned {}", *target_.process_id());
    loop {
        let input = p.read_input();
        let parts: Vec<String> = input.split(" ").map(|item| item.to_owned()).collect();
        let cmd = parts.get(0).map(|i| i.clone()).unwrap_or("".to_owned());
        match &cmd[..] {
            "q" | "quit" => {
                p.display_output("quitting");
                return Ok(());
            }
            "r" | "run" => {
                target_.continue_execution();
            }
            "b" | "breakpoint" => {
                let params = &parts[1..];
                if params.len() < 1 {
                    p.display_output(
                        "breakpoint command requires parameters: <address | function | symbol | source location>",
                    );
                } else {
                    let res = parse_hex_string(&params[0]);
                    if let Ok(addr) = res {
                        let addr = Address(addr);
                        if let Ok(_) =
                            target_.set_breakpoint(midas::software_breakpoint::BreakpointRequest::Address(addr))
                        {
                            p.display_output(&format!("Breakpoint set @ {:X?}", addr));
                        } else {
                            p.display_output("Failed to set breakpoint");
                        }
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
