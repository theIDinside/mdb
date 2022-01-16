use crate::ansicodes;
use crate::cfg;
use crate::key;

pub use super::fmt::*;
use crate::key::KeyboardInput;
struct PromptHistory {
    history: Vec<String>,
    history_selector: usize,
}

impl PromptHistory {
    pub fn new() -> PromptHistory {
        PromptHistory {
            history: Vec::with_capacity(20),
            history_selector: 0,
        }
    }

    pub fn add(&mut self, item: String) {
        self.history.push(item);
        self.history_selector = self.history.len();
    }

    pub fn get_previous(&mut self) -> Option<&String> {
        if self.history_selector == 0 {
            self.history_selector = self.history.len();
            None
        } else {
            self.history_selector -= 1;
            self.history.get(self.history_selector)
        }
    }

    pub fn get_next(&mut self) -> Option<&String> {
        self.history_selector += 1;
        self.history_selector = self.history_selector % (self.history.len() + 1);
        self.history.get(self.history_selector)
    }

    pub fn pop(&mut self) -> Option<String> {
        self.history.pop()
    }
}

pub struct Prompt {
    prompt: String,
    history: PromptHistory,
    cursor_column: usize,
    config: cfg::TerminalConfiguration,
}

// utility functions
pub(crate) fn write_string(s: &str) {
    unsafe {
        if libc::write(libc::STDOUT_FILENO, s.as_ptr() as _, s.len()) == -1 {
            panic!("failed to write to stdout");
        }
    }
}

pub(crate) fn move_cursor_left(steps: usize) {
    if steps != 0 {
        let cmd = format!("\x1b[{}D", steps);
        unsafe {
            if libc::write(libc::STDOUT_FILENO, cmd.as_ptr() as _, cmd.len()) == -1 {
                panic!("failed to write to stdout");
            }
        }
    }
}

pub(crate) fn move_cursor_right(steps: usize) {
    if steps != 0 {
        let cmd = format!("\x1b[{}C", steps);
        unsafe {
            if libc::write(libc::STDOUT_FILENO, cmd.as_ptr() as _, cmd.len()) == -1 {
                panic!("failed to write to stdout");
            }
        }
    }
}

impl Prompt {
    pub fn new<S: Into<String>>(prompt: S) -> Result<Prompt, &'static str> {
        let config = cfg::TerminalConfiguration::init()?;
        Ok(Prompt {
            prompt: format!("\r\x1b[2K{}", prompt.into()),
            history: PromptHistory::new(),
            cursor_column: 0,
            config,
        })
    }

    fn close(&mut self) {
        use libc::tcsetattr;
        if let Some(original_settings) = self.config.original {
            unsafe {
                if tcsetattr(0, libc::TCSAFLUSH, &original_settings as *const _ as _) == -1 {
                    println!("failed to restore terminal settings");
                }
            }
        }
    }

    pub fn prompt(&self) -> &str {
        &self.prompt[4..]
    }

    pub fn set_prompt(&mut self, prompt: &str) {
        self.prompt = format!("\x1b[2K{}", prompt);
    }

    pub fn read_input(&mut self) -> String {
        write_string(ansicodes::CLEAR_LINE);
        write_string(&self.prompt);

        let update_cursor = |current_input: &str, cursor_idx| {
            move_cursor_left(current_input.len().saturating_sub(cursor_idx));
        };
        self.cursor_column = 0;
        let mut res = String::with_capacity(100);
        loop {
            let read_key = key::read_key();
            match read_key.key {
                KeyboardInput::Char(c) => {
                    if self.cursor_column == res.len() {
                        unsafe {
                            if libc::write(libc::STDOUT_FILENO, &c as *const _ as _, 1) == -1 {
                                panic!("failed to write to stdout");
                            }
                        }
                        res.push(c);
                        self.cursor_column += 1;
                    } else {
                        res.insert(self.cursor_column, c);
                        write_string(&res[self.cursor_column..]);
                        self.cursor_column += 1;
                    }
                    if self.cursor_column != res.len() {
                        update_cursor(&res, self.cursor_column);
                    }
                }
                KeyboardInput::Left => {
                    if let Some(_) = read_key.modifier {
                        let p = res.len().saturating_sub(self.cursor_column);
                        let steps_left = res
                            .chars()
                            .rev()
                            .skip(p)
                            .position(|c| c == ' ')
                            .unwrap_or(self.cursor_column);
                        self.cursor_column = self.cursor_column.saturating_sub(steps_left);
                        move_cursor_left(steps_left);
                    } else {
                        move_cursor_left(1);
                        self.cursor_column = self.cursor_column.saturating_sub(1);
                    }
                }
                KeyboardInput::Right => {
                    if self.cursor_column != res.len() {
                        move_cursor_right(1);
                    }
                    self.cursor_column = self.cursor_column.saturating_add(1).min(res.len());
                }
                KeyboardInput::Up => {
                    write_string(ansicodes::CLEAR_LINE);
                    write_string(&self.prompt);
                    if let Some(s) = self.history.get_previous() {
                        res = s.clone();
                    } else {
                        res.clear();
                    }
                    self.cursor_column = res.len();
                    write_string(&res);
                }
                KeyboardInput::Down => {
                    write_string(ansicodes::CLEAR_LINE);
                    write_string(&self.prompt);
                    if let Some(s) = self.history.get_next() {
                        res = s.clone();
                    } else {
                        res.clear();
                    }
                    self.cursor_column = res.len();
                    write_string(&res);
                }
                KeyboardInput::PageUp => todo!("page up not yet implemented"),
                KeyboardInput::PageDown => todo!("page down not yet implemented"),
                KeyboardInput::Home => todo!("home not yet implemented"),
                KeyboardInput::End => todo!("end not yet implemented"),
                KeyboardInput::Enter => {
                    if !res.is_empty() {
                        write_string("\r\n");
                        self.history.add(res.clone());
                        return res;
                    }
                }
                KeyboardInput::Escape => todo!("escape not yet implemented"),
                KeyboardInput::Delete => {
                    if res.len() != 0 && self.cursor_column < res.len() {
                        res.remove(self.cursor_column);
                        write_string(ansicodes::CLEAR_LINE_FROM_CURSOR);
                        if res.is_empty() {
                            move_cursor_right(1);
                        }
                        write_string(&res[self.cursor_column..]);
                        update_cursor(&res, self.cursor_column);
                    }
                }
                KeyboardInput::Backspace => {
                    if let Some(_) = read_key.modifier {
                        if !res.is_empty() && self.cursor_column != 0 {
                            let skip = res.len().saturating_sub(self.cursor_column);
                            let mut remove_count = self.cursor_column;
                            for (i, x) in res.chars().rev().skip(skip).enumerate() {
                                if x == ' ' {
                                    remove_count = i;
                                    break;
                                }
                            }
                            unsafe {
                                std::ptr::copy(
                                    res.as_mut_ptr().offset(self.cursor_column as _) as _,
                                    res.as_mut_ptr()
                                        .offset(self.cursor_column as isize - remove_count as isize)
                                        as _,
                                    res.len().saturating_sub(self.cursor_column),
                                )
                            }
                            for _ in 0..remove_count {
                                res.pop();
                            }
                            self.cursor_column = self.cursor_column.saturating_sub(remove_count);
                            move_cursor_left(remove_count);
                            write_string(ansicodes::CLEAR_LINE_FROM_CURSOR);
                            write_string(&res[self.cursor_column..]);
                            update_cursor(&res, self.cursor_column);
                        }
                    } else {
                        if self.cursor_column == res.len() && !res.is_empty() {
                            res.pop();
                            self.cursor_column = self.cursor_column.saturating_sub(1);
                            move_cursor_left(1);
                            write_string(ansicodes::CLEAR_LINE_FROM_CURSOR);
                        } else {
                            if self.cursor_column > 0 {
                                self.cursor_column -= 1;
                                res.remove(self.cursor_column);
                                move_cursor_left(1);
                                write_string(ansicodes::CLEAR_LINE_FROM_CURSOR);
                                write_string(&res[self.cursor_column..]);
                                update_cursor(&res, self.cursor_column);
                            }
                        }
                    }
                }
                KeyboardInput::Err => todo!("err not yet implemented"),
            }
        }
    }

    pub fn display_string(&mut self, output: &str) {
        write_string(output);
        println!("");
    }

    pub fn display_error(&mut self, output: &str) {
        let mut fmt_buf = FormattedBuffer::with_capacity(output.len() + 25);
        fmt_buf.add_formatted_line(output, Format::new_with(Style::Bold, TextColor::Red));
        self.display_bytes_ref(&fmt_buf.output);
    }

    /// Writes `output` to terminal and appends a newline
    pub fn display_bytes_newline(&mut self, output: Vec<u8>) {
        unsafe {
            if libc::write(libc::STDOUT_FILENO, output.as_ptr() as _, output.len()) == -1 {
                panic!("failed to write to stdout");
            }
        }
        write_string("\r\n");
    }

    /// Writes `output` to terminal
    pub fn display_bytes(&mut self, output: Vec<u8>) {
        unsafe {
            if libc::write(libc::STDOUT_FILENO, output.as_ptr() as _, output.len()) == -1 {
                panic!("failed to write to stdout");
            }
        }
    }

    pub fn display_bytes_ref(&mut self, output: &Vec<u8>) {
        unsafe {
            if libc::write(libc::STDOUT_FILENO, output.as_ptr() as _, output.len()) == -1 {
                panic!("failed to write to stdout");
            }
        }
    }

    pub fn pop_history(&mut self) -> Option<String> {
        self.history.pop()
    }
}

impl Drop for Prompt {
    fn drop(&mut self) {
        self.close();
    }
}
