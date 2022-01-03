mod elf32;
mod elf64;

const EI_NIDENT: usize = 16;

use std::io::{Read, Seek, SeekFrom};

#[cfg(target_arch = "x86")]
pub use elf32::*;

#[cfg(target_arch = "x86_64")]
pub use elf64::*;
use nixwrap::MidasSysResult;

use crate::utils::midas_err;

pub struct Object {
    pub data: Vec<u8>,
    pub bytes_read: usize,
}

impl Object {
    pub fn new(data: Vec<u8>, bytes_read: usize) -> Self {
        Self {
            data: data,
            bytes_read: bytes_read,
        }
    }
}

pub fn load_object(path: &std::path::Path) -> nixwrap::MidasSysResult<Object> {
    let mut buf = vec![];
    let mut f = std::fs::OpenOptions::new()
        .read(true)
        .create_new(false)
        .open(path)
        .map_err(midas_err)?;
    let file_size = f.metadata().map_err(midas_err)?.len();
    buf.reserve(file_size as _);
    let bytes_read = f.read_to_end(&mut buf).map_err(midas_err)?;
    Ok(Object::new(buf, bytes_read))
}
