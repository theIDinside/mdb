use crate::{MidasSysResultDynamic, Pid};
pub(in crate) use libc::ptrace;
#[derive(Debug)]
pub struct UserRegisters {
    pub r15: u64,
    pub r14: u64,
    pub r13: u64,
    pub r12: u64,
    pub rbp: u64,
    pub rbx: u64,
    pub r11: u64,
    pub r10: u64,
    pub r9: u64,
    pub r8: u64,
    pub rax: u64,
    pub rcx: u64,
    pub rdx: u64,
    pub rsi: u64,
    pub rdi: u64,
    pub orig_rax: u64,
    pub rip: u64,
    pub cs: u64,
    pub eflags: u64,
    pub rsp: u64,
    pub ss: u64,
    pub fs_base: u64,
    pub gs_base: u64,
    pub ds: u64,
    pub es: u64,
    pub fs: u64,
    pub gs: u64,
}

impl UserRegisters {
    pub fn from(regs: libc::user_regs_struct) -> UserRegisters {
        let libc::user_regs_struct {
            r15,
            r14,
            r13,
            r12,
            rbp,
            rbx,
            r11,
            r10,
            r9,
            r8,
            rax,
            rcx,
            rdx,
            rsi,
            rdi,
            orig_rax,
            rip,
            cs,
            eflags,
            rsp,
            ss,
            fs_base,
            gs_base,
            ds,
            es,
            fs,
            gs,
        } = regs;
        UserRegisters {
            r15,
            r14,
            r13,
            r12,
            rbp,
            rbx,
            r11,
            r10,
            r9,
            r8,
            rax,
            rcx,
            rdx,
            rsi,
            rdi,
            orig_rax,
            rip,
            cs,
            eflags,
            rsp,
            ss,
            fs_base,
            gs_base,
            ds,
            es,
            fs,
            gs,
        }
    }

    pub fn pc(&self) -> u64 {
        self.rip
    }
}

pub fn init_user_regs() -> libc::user_regs_struct {
    libc::user_regs_struct {
        r15: 0,
        r14: 0,
        r13: 0,
        r12: 0,
        rbp: 0,
        rbx: 0,
        r11: 0,
        r10: 0,
        r9: 0,
        r8: 0,
        rax: 0,
        rcx: 0,
        rdx: 0,
        rsi: 0,
        rdi: 0,
        orig_rax: 0,
        rip: 0,
        cs: 0,
        eflags: 0,
        rsp: 0,
        ss: 0,
        fs_base: 0,
        gs_base: 0,
        ds: 0,
        es: 0,
        fs: 0,
        gs: 0,
    }
}

/** Kills the target debuggee if Midas debugger exits
 * pid - the process id which this should be true for
 * returns - foo
*/
pub fn kill_on_midas_exit(pid: Pid) -> Result<(), String> {
    use libc::PTRACE_SETOPTIONS;
    unsafe {
        if ptrace(
            PTRACE_SETOPTIONS,
            *pid,
            std::ptr::null() as *const libc::c_void,
            std::ptr::null() as *const libc::c_void,
        ) == -1
        {
            Err(crate::errno::get_errno_msg())
        } else {
            Ok(())
        }
    }
}

pub fn trace_me() -> crate::MidasSysResultDynamic<()> {
    unsafe {
        if ptrace(
            libc::PTRACE_TRACEME,
            0,
            std::ptr::null() as *const libc::c_void,
            std::ptr::null() as *const libc::c_void,
        ) == -1
        {
            Err(crate::errno::get_errno_msg())
        } else {
            Ok(())
        }
    }
}

pub fn peek_data(pid: Pid, addr: usize) -> crate::MidasSysResultDynamic<i64> {
    unsafe {
        let quadword = libc::ptrace(
            libc::PTRACE_PEEKDATA,
            *pid,
            addr,
            std::ptr::null() as *const libc::c_void,
        );
        if quadword == -1 {
            Err(format!(
                "failed to peek data at {} of [PID: {}]",
                addr, *pid
            ))
        } else {
            Ok(quadword)
        }
    }
}

pub fn poke_data(pid: Pid, addr: usize, data: libc::c_long) -> MidasSysResultDynamic<i64> {
    unsafe {
        let written_bytes = libc::ptrace(libc::PTRACE_POKEDATA, *pid, addr, data);
        if written_bytes == -1 {
            Err(crate::errno::get_errno_msg())
        } else {
            Ok(written_bytes)
        }
    }
}

pub fn get_regs(pid: Pid) -> UserRegisters {
    let mut regs = init_user_regs();
    unsafe {
        libc::ptrace(
            libc::PTRACE_GETREGS,
            *pid,
            std::ptr::null() as *const libc::c_void,
            &mut regs as *mut _,
        );
    }
    UserRegisters::from(regs)
}

pub fn set_pc(pid: Pid, address: usize) -> MidasSysResultDynamic<()> {
    let mut regs = init_user_regs();
    unsafe {
        if libc::ptrace(
            libc::PTRACE_GETREGS,
            *pid,
            std::ptr::null() as *const libc::c_void,
            &mut regs as *mut _,
        ) == -1
        {
            return Err(crate::errno::get_errno_msg());
        }
    };
    regs.rip = address as _;
    unsafe {
        if libc::ptrace(
            libc::PTRACE_SETREGS,
            *pid,
            std::ptr::null() as *const libc::c_void,
            &regs,
        ) == -1
        {
            return Err(crate::errno::get_errno_msg());
        }
    }
    Ok(())
}
