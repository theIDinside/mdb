use crate::{MidasSysResultDynamic, Pid};
pub(in crate) use libc::ptrace;

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

pub fn poke_data(pid: Pid, addr: usize, data: libc::c_long) -> MidasSysResultDynamic<()> {
    unsafe {
        if libc::ptrace(libc::PTRACE_POKEDATA, *pid, addr, data) == -1 {
            Err(crate::errno::get_errno_msg())
        } else {
            Ok(())
        }
    }
}
