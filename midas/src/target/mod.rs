#![allow(unused, non_camel_case_types)]
use nixwrap::{Pid, WaitStatus};
pub mod linux;

use nixwrap::MidasSysResultDynamic;

use crate::software_breakpoint::BreakpointRequest;
use crate::types::Address;

pub struct MemoryRead {
    pub result: Vec<Vec<u8>>,
    bytes_read: usize,
}

impl MemoryRead {
    pub fn read_memory(pid: Pid, ranges: Vec<(Address, usize)>) -> MidasSysResultDynamic<MemoryRead> {
        // the iovecs, containing a { pointer to a buffer where the bytes should be read from, and the length }
        let mut read_parameters = Vec::with_capacity(ranges.len());
        // the actual backing storage where we copy the data into. Each element in read_parameters, have a pointer, that points into this buffer of buffers
        let mut backing_storage: Vec<Vec<u8>> = Vec::with_capacity(ranges.len());
        // the iovecs, containing a { pointer to a buffer where the bytes should be copied to, and the length }
        let mut store_parameters = Vec::with_capacity(ranges.len());

        for (index, (addr, bytes)) in ranges.iter().enumerate() {
            // push is safe here; because we've allocated the vectors up front with_capacity, so *no* re-allocation or moving *should* happen
            backing_storage.push(Vec::with_capacity(*bytes));
            read_parameters.push(libc::iovec {
                iov_base: addr.value() as *mut _,
                iov_len: *bytes,
            });
            store_parameters.push(libc::iovec {
                iov_base: backing_storage.get_mut(index).unwrap().as_ptr() as _,
                iov_len: *bytes,
            });
        }

        unsafe {
            let bytes_read = libc::process_vm_readv(
                *pid,
                store_parameters.as_ptr() as _,
                store_parameters.len() as _,
                read_parameters.as_ptr() as _,
                read_parameters.len() as _,
                0,
            );
            if bytes_read == -1 {
                Err(nixwrap::errno::get_errno_msg())
            } else {
                Ok(MemoryRead {
                    result: backing_storage,
                    bytes_read: bytes_read as usize,
                })
            }
        }
    }
}

// represents the state operations we can do on the debuggeee
pub trait Target {
    fn launch(command: &mut std::process::Command) -> MidasSysResultDynamic<(Box<dyn Target>, WaitStatus)>
    where
        Self: Sized;
    fn process_id(&self) -> Pid;
    fn step(&mut self, steps: usize);
    fn continue_execution(&mut self) -> MidasSysResultDynamic<WaitStatus>;
    fn kill(&mut self) -> MidasSysResultDynamic<WaitStatus>;
    fn read_memory(&self, address: usize, bytes: usize) -> Vec<u8>;
    fn kill_on_tracer_exit(&mut self) -> MidasSysResultDynamic<()>;
    fn set_breakpoint(&mut self, bp: BreakpointRequest) -> MidasSysResultDynamic<()>;
}

pub fn make_command(program_path: &str, args: Vec<&str>) -> MidasSysResultDynamic<std::process::Command> {
    let program = std::path::Path::new(program_path);
    if !program.exists() {
        Err(format!("{} doesn't exist", program.display()))
    } else {
        let mut command = std::process::Command::new(program);
        command.args(args.iter());
        Ok(command)
    }
}
