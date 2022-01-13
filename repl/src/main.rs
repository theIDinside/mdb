extern crate cli;
extern crate linuxwrapper as nixwrap;
extern crate midas;

use midas::target::{self, Target};

use crate::commands::parse_user_input;
mod commands;

#[derive(Debug)]
pub enum CommandResultError {
    SymbolNotFound(String),
    MidasLibraryError(String),
}

impl From<midas::MidasError> for CommandResultError {
    fn from(e: midas::MidasError) -> Self {
        CommandResultError::MidasLibraryError(format!("Error: {}", e.describe()))
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
    let separator = args.iter().position(|item| item == "--");
    let inferiors_args: Vec<&str> = if let Some(pos) = separator {
        args.iter().skip(pos).map(|s| s.as_str()).collect()
    } else {
        Vec::new()
    };

    let program_path = args
        .get(1)
        .ok_or("You did not provide a binary".to_owned())?;
    let mut prompt = cli::Prompt::new("midas> ")?;
    let object = midas::elf::load_object(std::path::Path::new(program_path))
        .map_err(|_| "Failed to load binary to parse for information".to_string())?;
    let _elf = midas::elf::ParsedELF::parse_elf(object.clone()).map_err(|e| format!("{}", e.describe()))?;

    let (mut target_, _waitstatus) =
        midas::target::linux::LinuxTarget::launch(&mut target::make_command(program_path, inferiors_args).unwrap())
            .unwrap();
    {
        let mut f = Formatted::with_capacity(35);
        f.add_with_format(
            &format!("spawned {}", *target_.process_id()),
            Format::new().color(TextColor::Green).style(Style::Bold),
        );
        prompt.display_formatted(f.output);
    }
    loop {
        let input = prompt.read_input();
        match parse_user_input(&input) {
            commands::ReplCommands::Quit => {
                prompt.display_output("quitting");
                return Ok(());
            }
            commands::ReplCommands::Run => match target_.continue_execution() {
                Ok(_status) => {
                    if let Some(msg) = prepare_waitstatus_display_message(_status, target_.as_mut()) {
                        let m = msg;
                        prompt.display_formatted(m.output);
                    }
                }
                Err(err) => prompt.display_output(&err),
            },
            commands::ReplCommands::SetBreakpoint(maybe_parsed) => match maybe_parsed {
                Ok(bp_req) => match target_.set_breakpoint(bp_req) {
                    Ok(addr) => {
                        let mut format = Formatted::new();
                        format.add_formatted(&format!("Breakpoint set @ {:X?}", addr), TextColor::Green);
                        prompt.display_formatted(format.output);
                    }
                    Err(err_msg) => {
                        let mut format = Formatted::new();
                        format.add_formatted(
                            &format!("Failed to set breakpoint: {}", err_msg),
                            TextColor::Red,
                        );
                        prompt.display_formatted(format.output);
                    }
                },
                Err(err_msg) => prompt.display_output(&format!("Failed to set breakpoint: {}", err_msg)),
            },
            commands::ReplCommands::UnknownCommand => prompt.display_output("Unkonwn command"),
        };
    }
}

pub enum Style {
    Bold,
    Underlined,
}

pub enum TextColor {
    Red,
    Green,
}

pub struct Format {
    color: Option<TextColor>,
    style: Option<Style>,
}

impl Format {
    pub fn new() -> Format {
        Format {
            color: None,
            style: None,
        }
    }

    pub fn new_with(style: Style, color: TextColor) -> Format {
        Format {
            color: Some(color),
            style: Some(style),
        }
    }

    pub fn style(mut self, style: Style) -> Self {
        self.style = Some(style);
        self
    }

    pub fn color(mut self, color: TextColor) -> Self {
        self.color = Some(color);
        self
    }

    pub fn make(&self) -> Vec<u8> {
        let mut v: Vec<u8> = Vec::with_capacity("\x1b[31\x1b[31".len());
        v.extend_from_slice(
            &self
                .color
                .as_ref()
                .map(|c| Into::<&str>::into(c).as_bytes())
                .unwrap_or("".as_bytes()),
        );
        v.extend_from_slice(
            self.style
                .as_ref()
                .map(|s| Into::<&str>::into(s).as_bytes())
                .unwrap_or("".as_bytes()),
        );
        v
    }
}

impl Into<&'static str> for Style {
    fn into(self) -> &'static str {
        match self {
            Style::Bold => "\x1b[1m",
            Style::Underlined => "\x1b[4m",
        }
    }
}

impl Into<&'static str> for TextColor {
    fn into(self) -> &'static str {
        match self {
            TextColor::Red => "\x1b[31m",
            TextColor::Green => "\x1b[32m",
        }
    }
}

impl Into<&'static str> for &Style {
    fn into(self) -> &'static str {
        match self {
            Style::Bold => "\x1b[1m",
            Style::Underlined => "\x1b[4m",
        }
    }
}

impl Into<&'static str> for &TextColor {
    fn into(self) -> &'static str {
        match self {
            TextColor::Red => "\x1b[31m",
            TextColor::Green => "\x1b[32m",
        }
    }
}

pub struct Formatted {
    pub output: Vec<u8>,
}

impl Formatted {
    pub fn new() -> Formatted {
        Formatted { output: vec![] }
    }

    pub fn with_capacity(capacity: usize) -> Formatted {
        Formatted {
            output: Vec::with_capacity(capacity),
        }
    }

    pub fn add_unformatted<S: AsRef<str>>(&mut self, data: S) {
        self.output.extend_from_slice(data.as_ref().as_bytes());
    }

    pub fn add_with_format<S: AsRef<str>>(&mut self, string: &S, fmt: Format) {
        self.output.extend_from_slice(&fmt.make());
        self.output.extend_from_slice(string.as_ref().as_bytes());
        self.output.extend_from_slice("\x1b[00m".as_bytes());
    }

    pub fn add_formatted<S: AsRef<str>>(&mut self, string: &S, color: TextColor) {
        self.output
            .extend_from_slice(Into::<&'static str>::into(color).as_bytes());
        self.output.extend_from_slice(string.as_ref().as_bytes());
        self.output.extend_from_slice("\x1b[00m".as_bytes());
    }
}

#[allow(unused)]
fn prepare_waitstatus_display_message(_status: nixwrap::WaitStatus, target: &dyn Target) -> Option<Formatted> {
    let mut format = Formatted::new();
    format.add_unformatted("\n");
    match _status {
        nixwrap::WaitStatus::Continued(pid) => {
            format.add_formatted(&"Inferior continued", TextColor::Green);
            return Some(format);
        }
        nixwrap::WaitStatus::ExitedNormally(pid, exit_code) => {
            let mut f = Format::new().color(TextColor::Red).style(Style::Bold);
            format.add_with_format(
                &format!("Inferior exited normally with exit code {}", exit_code),
                f,
            );
            return Some(format);
        }
        nixwrap::WaitStatus::Stopped(pid, signal) => match signal {
            nixwrap::signals::Signal::HangUp => todo!(),
            nixwrap::signals::Signal::Interrupt => todo!(),
            nixwrap::signals::Signal::Quit => todo!(),
            nixwrap::signals::Signal::Ill => todo!(),
            nixwrap::signals::Signal::Trap => {
                if let Some(addr) = target.stopped_at_breakpoint() {
                    format.add_formatted(&format!("Hit breakpoint @ {:X?}", addr), TextColor::Green);
                    return Some(format);
                } else {
                    format.add_unformatted("Caught trap signal");
                    return Some(format);
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
            format.add_formatted(
                &format!("Inferior killed with signal {:?}", signal),
                TextColor::Red,
            );
            return Some(format);
        }
        nixwrap::WaitStatus::CoreDumped(pid) => {
            format.add_formatted(&"Core dumped", TextColor::Red);
            return Some(format);
        }
    }
}
