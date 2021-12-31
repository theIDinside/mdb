use midas::commands::Command;

pub fn parse_input(input: String) -> Command {
    match &input[..] {
        "info" | "i" => Command::Info,
        _ => Command::Unknown,
    }
}
