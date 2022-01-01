use crate::Pid;
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

pub fn trace_me() -> crate::MidasSysResult<()> {
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
