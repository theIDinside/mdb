extern crate cli;

fn main() -> Result<(), &'static str> {
    let mut p = cli::Prompt::new("midas> ")?;
    loop {
        let input = p.read_input();
        match &input[..] {
            "q" | "quit" => {
                p.display_output("quitting");
                return Ok(());
            }
            _ => {
                p.display_output(&format!("You wrote: {}", input));
            }
        }
    }
}
