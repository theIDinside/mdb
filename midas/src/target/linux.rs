use nixwrap::Pid;
use std::collections::{HashMap, HashSet};

use crate::{software_breakpoint::Breakpoint, types::Address};

pub struct LinuxTarget {
    binary: String,
    pid: Pid,
    software_breakpoints: HashMap<Address, HashSet<Breakpoint>>,
}
