#[allow(dead_code)]

pub const CLEAR_SCREEN: &'static str = "\x1b[2J";

pub const CLEAR_LINE_FROM_CURSOR: &'static str = "\x1b[0K";
pub const CLEAR_LINE_TO_CURSOR: &'static str = "\x1b[1K";
pub const CLEAR_LINE: &'static str = "\x1b[2K";

pub const SET_BOLD: &'static str = "\x1b[1m";
pub const SET_DIM: &'static str = "\x1b[2m";
pub const COLOR_BLACK: (&'static str, &'static str) = ("30", "40");
pub const COLOR_RED: (&str, &str) = ("31", "41");
pub const COLOR_GREEN: (&str, &str) = ("32", "42");
pub const COLOR_WHITE: (&str, &str) = ("37", "37");
pub const RESET: &str = "0";
pub const LAST_LINE: &str = "\x1b[99999B";

pub const RESET_PROMPT: &str = "\x1b[9999B\x1b[2Jmdb>";
