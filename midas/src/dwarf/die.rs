#![allow(unused, non_camel_case_types)]
pub struct DIE {
    tag: usize,
}

pub enum DIEHasChildren {
    No = 0x00,
    Yes = 0x01,
}
