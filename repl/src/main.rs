extern crate cli;
extern crate linuxwrapper as nixwrap;
extern crate midas;
use midas::{
    target::{self, Target},
    types::Address,
    ELFSection,
};
mod commands;

#[derive(Debug)]
pub enum CommandResultError {
    SymbolNotFound(String),
    MidasLibraryError(String),
}

impl From<midas::MidasError> for CommandResultError {
    fn from(e: midas::MidasError) -> Self {
        CommandResultError::MidasLibraryError(format!("Error: {}", e.description()))
    }
}

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
    let _elf = midas::elf::ParsedELF::parse_elf(&object).map_err(|e| format!("{}", e.description()))?;

    let (mut target_, _waitstatus) =
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
            "r" | "run" => match target_.continue_execution() {
                Ok(_status) => {
                    if let Some(msg) = prepare_waitstatus_display_message(_status, target_.as_mut()) {
                        p.display_output(&msg);
                    }
                }
                Err(err) => p.display_output(&err),
            },
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
                        let find_address_of_symbol = |name| {
                            midas::find_low_pc_of(
                                name,
                                _elf.get_dwarf_section(midas::dwarf::Section::DebugInfo)?,
                                _elf.get_dwarf_section(midas::dwarf::Section::DebugPubNames)?,
                                _elf.get_dwarf_section(midas::dwarf::Section::DebugAbbrev)?,
                            )
                            .ok_or(CommandResultError::SymbolNotFound(format!(
                                "{} not found",
                                &name
                            )))
                        };

                        if let Some(addr) = _elf
                            .symbol_table
                            .get_function_symbol(&params[0])
                            .and_then(|s| s.value.map(|v| Address(v.get())))
                        {
                            if let Ok(_) =
                                target_.set_breakpoint(midas::software_breakpoint::BreakpointRequest::Address(addr))
                            {
                                p.display_output(&format!("Breakpoint set @ {:X?}", addr))
                            } else {
                                p.display_output("Failed to set breakpoint");
                            }
                        } else {
                            match find_address_of_symbol(&params[0]) {
                                Ok(addr) => {
                                    if let Ok(_) = target_.set_breakpoint(
                                        midas::software_breakpoint::BreakpointRequest::Address(Address(addr)),
                                    ) {
                                        p.display_output(&format!("Breakpoint set @ {:X?}", addr))
                                    } else {
                                        p.display_output("Failed to set breakpoint");
                                    }
                                }
                                Err(err) => {
                                    p.display_output(&format!("Failed: {:?}", err));
                                }
                            }
                        }
                    }
                }
            }
            _ => {
                p.display_output(&format!("You wrote: {}", input));
            }
        }
    }
}
#[allow(unused)]
fn prepare_waitstatus_display_message(_status: nixwrap::WaitStatus, target: &dyn Target) -> Option<String> {
    match _status {
        nixwrap::WaitStatus::Continued(pid) => {
            return Some(format!("Inferior continued"));
        }
        nixwrap::WaitStatus::ExitedNormally(pid, exit_code) => {
            return Some(format!(
                "Inferior exited normally with exit code {}",
                exit_code
            ));
        }
        nixwrap::WaitStatus::Stopped(pid, signal) => match signal {
            nixwrap::signals::Signal::HangUp => todo!(),
            nixwrap::signals::Signal::Interrupt => todo!(),
            nixwrap::signals::Signal::Quit => todo!(),
            nixwrap::signals::Signal::Ill => todo!(),
            nixwrap::signals::Signal::Trap => {
                if let Some(addr) = target.stopped_at_breakpoint() {
                    return Some(format!("Hit breakpoint @ {:X?}", addr));
                } else {
                    return Some(format!("Caught trap signal"));
                }
            }
            nixwrap::signals::Signal::Abort => todo!(),
            nixwrap::signals::Signal::BusError => todo!(),
            nixwrap::signals::Signal::FloatingPointException => todo!(),
            nixwrap::signals::Signal::Kill => todo!(),
            nixwrap::signals::Signal::UserDefined1 => todo!(),
            nixwrap::signals::Signal::SegmentationFault => todo!(),
            nixwrap::signals::Signal::UserDefined2 => todo!(),
            nixwrap::signals::Signal::BrokenPipe => todo!(),
            nixwrap::signals::Signal::Alarm => todo!(),
            nixwrap::signals::Signal::Termination => todo!(),
            nixwrap::signals::Signal::StackFault => todo!(),
            nixwrap::signals::Signal::ChildStopped => todo!(),
            nixwrap::signals::Signal::Continued => todo!(),
            nixwrap::signals::Signal::Stopped => todo!(),
            nixwrap::signals::Signal::SignalTerminalStop => todo!(),
            nixwrap::signals::Signal::TTYIn => todo!(),
            nixwrap::signals::Signal::TTYOut => todo!(),
            nixwrap::signals::Signal::UrgentOutOfBand => todo!(),
            nixwrap::signals::Signal::CPUTimeLimitExceeded => todo!(),
            nixwrap::signals::Signal::FileSizeExceeded => todo!(),
            nixwrap::signals::Signal::VirtualTimeAlarm => todo!(),
            nixwrap::signals::Signal::ProfilingTimerExpired => todo!(),
            nixwrap::signals::Signal::WindowsChange => todo!(),
            nixwrap::signals::Signal::InputOutputPoll => todo!(),
            nixwrap::signals::Signal::PowerFailure => todo!(),
            nixwrap::signals::Signal::BadSystemCallArgument => todo!(),
        },
        nixwrap::WaitStatus::Killed(pid, signal) => {
            return Some(format!("Inferior killed with signal {:?}", signal));
        }
        nixwrap::WaitStatus::CoreDumped(pid) => {
            return Some(format!("Core dumped"));
        }
    }
}
