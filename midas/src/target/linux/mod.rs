use std::{
    collections::{HashMap, HashSet},
    io::Read,
    os::unix::prelude::CommandExt,
};

use crate::{
    software_breakpoint::{self, Breakpoint},
    types::Address,
    CommandErrors, MidasError, MidasSysResult,
};
use nixwrap::{waitpid, MidasSysResultDynamic, Pid, WaitStatus};

pub mod debug_info;

pub struct LinuxTarget {
    _binary: String,
    pid: Pid,
    _software_breakpoints: HashMap<Address, Vec<Breakpoint>>,
    debug_info: debug_info::DebugInfo,
}

impl super::Target for LinuxTarget {
    fn launch(command: &mut std::process::Command) -> MidasSysResultDynamic<(Box<dyn super::Target>, WaitStatus)> {
        let pathstr = command.get_program().to_owned();
        let path = std::path::Path::new(&pathstr);
        let debug_info = debug_info::DebugInfo::new(path);
        if !path.exists() {
            Err(format!("binary {} could not be found", path.display()))
        } else {
            unsafe {
                // this closure executes in the forked child code. So in a "regular" old fork situation
                // we would check pid if == 0 or something similar, and then handle accordingly. This closure always execs in the child.
                command.pre_exec(|| {
                    #[cfg(target_os = "linux")]
                    {
                        if libc::personality(libc::ADDR_NO_RANDOMIZE as _) == -1 {
                            panic!("Setting no randomized virtual memory failed");
                        }
                        nixwrap::ptrace::trace_me()
                            .map_err(|err_string| std::io::Error::new(std::io::ErrorKind::Other, err_string))?;
                        Ok(())
                    }
                });
                let child = command
                    .spawn()
                    .map_err(|err| format!("Spawning child failed: {}", err))?;
                let pid = Pid(child.id() as _);
                let status = waitpid(*pid, 0)?;
                let target = Box::new(LinuxTarget {
                    _binary: path.to_str().unwrap().to_string(),
                    pid: pid,
                    _software_breakpoints: HashMap::new(),
                    debug_info: debug_info.unwrap(),
                });
                Ok((target, status))
            }
        }
    }

    fn process_id(&self) -> Pid {
        self.pid
    }

    fn step(&mut self, _steps: usize) {
        todo!()
    }

    fn continue_execution(&mut self) -> nixwrap::MidasSysResultDynamic<nixwrap::WaitStatus> {
        let pc = nixwrap::ptrace::get_regs(self.process_id())
            .pc()
            .saturating_sub(1);
        let hit_breakpoint = match self._software_breakpoints.get_mut(&Address(pc as usize)) {
            Some(bp_set) => {
                let mut disabled = false;
                for bp in bp_set.iter_mut() {
                    if bp.enabled && !disabled {
                        bp.disable();
                        disabled = true;
                    }
                    bp.set_is_enabled(false);
                }
                true
            }
            _ => false,
        };
        if hit_breakpoint {
            nixwrap::ptrace::set_pc(self.process_id(), pc as usize);
        }
        nixwrap::continue_execution(*self.pid).unwrap();
        let opts = 0;
        nixwrap::waitpid(*self.pid, opts)
    }

    fn kill(&mut self) -> nixwrap::MidasSysResultDynamic<nixwrap::WaitStatus> {
        todo!()
    }

    fn read_memory(&self, _address: usize, _bytes: usize) -> Vec<u8> {
        todo!()
    }

    fn kill_on_tracer_exit(&mut self) -> nixwrap::MidasSysResultDynamic<()> {
        nixwrap::ptrace::kill_on_midas_exit(self.process_id())
    }

    fn set_breakpoint(&mut self, bp: crate::software_breakpoint::BreakpointRequest) -> MidasSysResultDynamic<Address> {
        match bp {
            crate::software_breakpoint::BreakpointRequest::Address(Address(addr)) => {
                let bp = super::super::software_breakpoint::Breakpoint::set_enabled(self.pid, addr)?;
                let key = Address(addr);
                if let Some(set) = self._software_breakpoints.get_mut(&key) {
                    set.push(bp);
                } else {
                    self._software_breakpoints.insert(key, vec![bp]);
                }
                Ok(key)
            }
            crate::software_breakpoint::BreakpointRequest::SourceCodeLocation { line, column, file } => {
                let line_number_table = self
                    .debug_info
                    .elf
                    .get_dwarf_section(crate::dwarf::Section::DebugLine)
                    .expect("failed to get .debug_line");

                let header_iterator = crate::dwarf::linenumber::ProgramHeaderIterator::new(
                    line_number_table,
                    crate::dwarf::linenumber::LineInstructionConfig {
                        pointer_width: 8,
                        opcode_base: 13,
                    },
                );

                let it = crate::dwarf::linenumber::TableIterator::new(
                    line_number_table,
                    header_iterator,
                    crate::dwarf::linenumber::LineInstructionConfig {
                        pointer_width: 8,
                        opcode_base: 13,
                    },
                );
                for mut program in it {
                    if let Some(pos) = program
                        .header
                        .file_names
                        .iter()
                        .position(|fe| fe.path == file)
                    {
                        let results = program.run();
                        for res in results {
                            let f = program
                                .header
                                .file_names
                                .get(res.file.saturating_sub(1) as usize)
                                .unwrap();
                            if f.path == file && res.line == line as _ {
                                let bp = software_breakpoint::Breakpoint::set_enabled(self.pid, res.address)?;
                                let key = Address(res.address);
                                if let Some(set) = self._software_breakpoints.get_mut(&key) {
                                    set.push(bp);
                                } else {
                                    self._software_breakpoints.insert(key, vec![bp]);
                                }
                                return Ok(key);
                            }
                        }
                    }
                }
                return Err("No address found to be set".to_string());
            }
            crate::software_breakpoint::BreakpointRequest::Function { name, file } => {
                let addr = crate::find_low_pc_of(
                    &name,
                    self.debug_info
                        .elf
                        .get_dwarf_section(crate::dwarf::Section::DebugInfo)
                        .map_err(|_| "failed to get dwarf section".to_string())?,
                    self.debug_info
                        .elf
                        .get_dwarf_section(crate::dwarf::Section::DebugPubNames)
                        .map_err(|_| "failed to get dwarf section".to_string())?,
                    self.debug_info
                        .elf
                        .get_dwarf_section(crate::dwarf::Section::DebugAbbrev)
                        .map_err(|_| "failed to get dwarf section".to_string())?,
                );
                if let Some(addr) = addr {
                    let bp = super::super::software_breakpoint::Breakpoint::set_enabled(self.pid, addr)?;
                    let key = Address(addr);
                    if let Some(set) = self._software_breakpoints.get_mut(&key) {
                        set.push(bp);
                    } else {
                        self._software_breakpoints.insert(key, vec![bp]);
                    }
                    Ok(key)
                } else {
                    Err(format!("Failed to get address of {}", name))
                }
            }
        }
    }

    fn stopped_at_breakpoint(&self) -> Option<Address> {
        let pc = nixwrap::ptrace::get_regs(self.process_id())
            .pc()
            .saturating_sub(1);
        if self
            ._software_breakpoints
            .get(&Address(pc as usize))
            .is_some()
        {
            Some(Address(pc as _))
        } else {
            None
        }
    }

    fn read_registers(&self) -> nixwrap::ptrace::UserRegisters {
        nixwrap::ptrace::get_regs(self.process_id())
    }

    fn source_code_at_pc(&self, lines: usize) -> MidasSysResult<(usize, Vec<(usize, String)>)> {
        let pc = self.read_registers().pc();
        let line_number_table = self
            .debug_info
            .elf
            .get_dwarf_section(crate::dwarf::Section::DebugLine)
            .expect("failed to get .debug_line");

        let header_iterator = crate::dwarf::linenumber::ProgramHeaderIterator::new(
            line_number_table,
            crate::dwarf::linenumber::LineInstructionConfig {
                pointer_width: 8,
                opcode_base: 13,
            },
        );

        let it = crate::dwarf::linenumber::TableIterator::new(
            line_number_table,
            header_iterator,
            crate::dwarf::linenumber::LineInstructionConfig {
                pointer_width: 8,
                opcode_base: 13,
            },
        );
        for mut program in it {
            let results = program.run();
            for res in results {
                if res.address == (pc - 1) as _ {
                    if let Some(p) = program.header.get_full_path_of_file(res.file as usize) {
                        let foo = &p;
                        let mut f = std::fs::File::open(p).map_err(|e| MidasError::FileOpenError(e.kind()))?;
                        let mut buf = String::with_capacity(
                            f.metadata()
                                .map_err(|e| MidasError::FileOpenError(e.kind()))?
                                .len() as usize,
                        );
                        let bytes = f
                            .read_to_string(&mut buf)
                            .map_err(|e| MidasError::FileReadError(e.kind()))?;
                        let ln = lines as u32;
                        let start = res.line.saturating_sub(ln / 2);
                        let end = res.line.saturating_add(ln / 2);
                        let content: Vec<(usize, String)> = buf
                            .lines()
                            .enumerate()
                            .skip(start as usize)
                            .take((end - start) as usize)
                            .map(|(line_index, s)| (line_index, s.to_string()))
                            .collect();
                        return Ok((res.line as usize, content));
                    }
                }
            }
        }
        Err(MidasError::ClientOperation(CommandErrors::ContextNotFound))
    }
}
