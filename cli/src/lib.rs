use libc::TCSAFLUSH;

extern crate libc;
pub(crate) mod ansicodes;
pub(crate) mod cfg;
pub(crate) mod key;

pub mod prompt;

pub use prompt::Prompt;
