/**
 * Enum mapping to the various signal nums in Linux. These names here, might change, as I get a better understanding of some of them or all of them.
 * For now, I've renamed them from their arcane shitty names, so that they can actually make sense when reading them, without having to man 7 signal every time.
 */
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Signal {
    // SIGHUP
    HangUp = 1,
    // SIGINT
    Interrupt = 2,
    // SIGQUIT
    Quit = 3,
    // SIGILL
    Ill = 4,
    // SIGTRAP
    Trap = 5,
    // SIGABRT
    Abort = 6,
    // SIGIOT
    // InputOutputTrap = 6,
    // SIGBUS
    BusError = 7,
    // SIGFPE
    FloatingPointException = 8,
    // SIGKILL
    Kill = 9,
    // SIGUSR1
    UserDefined1 = 10,
    // SIGSEGV
    SegmentationFault = 11,
    // SIGUSR2
    UserDefined2 = 12,
    // SIGPIPE
    BrokenPipe = 13,
    // SIGALRM
    Alarm = 14,
    // SIGTERM
    Termination = 15,
    // SIGSTKFLT
    StackFault = 16,
    // SIGCHLD
    ChildStopped = 17,
    // SIGCONT
    Continued = 18,
    // SIGSTOP
    Stopped = 19,
    // SIGTSTP
    SignalTerminalStop = 20,
    // SIGTTIN
    TTYIn = 21,
    // SIGTTOU
    TTYOut = 22,
    // SIGURG
    UrgentOutOfBand = 23,
    // SIGXCPU
    CPUTimeLimitExceeded = 24,
    // SIGXFSZ
    FileSizeExceeded = 25,
    // SIGVTALRM
    VirtualTimeAlarm = 26,
    // SIGPROF
    ProfilingTimerExpired = 27,
    // SIGWINCH
    WindowsChange = 28,
    // SIGIO / SIGPOLL
    InputOutputPoll = 29,
    // SIGPWR
    PowerFailure = 30,
    // SIGSYS
    BadSystemCallArgument = 31,
}

impl Signal {
    pub fn from_raw(signum: i32) -> Result<Signal, i32> {
        match signum {
            1 => Ok(Signal::HangUp),
            2 => Ok(Signal::Interrupt),
            3 => Ok(Signal::Quit),
            4 => Ok(Signal::Ill),
            5 => Ok(Signal::Trap),
            6 => Ok(Signal::Abort),
            7 => Ok(Signal::BusError),
            8 => Ok(Signal::FloatingPointException),
            9 => Ok(Signal::Kill),
            10 => Ok(Signal::UserDefined1),
            11 => Ok(Signal::SegmentationFault),
            12 => Ok(Signal::UserDefined2),
            13 => Ok(Signal::BrokenPipe),
            14 => Ok(Signal::Alarm),
            15 => Ok(Signal::Termination),
            16 => Ok(Signal::StackFault),
            17 => Ok(Signal::ChildStopped),
            18 => Ok(Signal::Continued),
            19 => Ok(Signal::Stopped),
            20 => Ok(Signal::SignalTerminalStop),
            21 => Ok(Signal::TTYIn),
            22 => Ok(Signal::TTYOut),
            23 => Ok(Signal::UrgentOutOfBand),
            24 => Ok(Signal::CPUTimeLimitExceeded),
            25 => Ok(Signal::FileSizeExceeded),
            26 => Ok(Signal::VirtualTimeAlarm),
            27 => Ok(Signal::ProfilingTimerExpired),
            28 => Ok(Signal::WindowsChange),
            29 => Ok(Signal::InputOutputPoll),
            30 => Ok(Signal::PowerFailure),
            31 => Ok(Signal::BadSystemCallArgument),
            _ => Err(signum),
        }
    }
}
