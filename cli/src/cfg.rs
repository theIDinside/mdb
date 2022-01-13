fn zeroed_termios() -> libc::termios {
    libc::termios {
        c_iflag: 0,
        c_oflag: 0,
        c_cflag: 0,
        c_lflag: 0,
        c_line: 0,
        c_cc: [0; 32],
        c_ispeed: 0,
        c_ospeed: 0,
    }
}

pub struct TerminalConfiguration {
    pub original: Option<libc::termios>,
    pub app_settings: Option<libc::termios>,
}

impl TerminalConfiguration {
    pub fn init() -> Result<TerminalConfiguration, &'static str> {
        use libc::{BRKINT, CS8, ECHO, ICANON, ICRNL, IEXTEN, INPCK, ISIG, ISTRIP, IXON};
        // non-portable code
        let stdin_fd = 0;
        let mut original_attributes = zeroed_termios();
        unsafe {
            if libc::tcgetattr(stdin_fd, &mut original_attributes as _) == -1 {
                return Err("Failed to get terminal attributes / settings");
            }
            let mut runtime_settings = original_attributes.clone();
            runtime_settings.c_iflag &= !(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
            // runtime_settings.c_oflag &= !(OPOST);
            runtime_settings.c_oflag |= libc::ONLCR;
            runtime_settings.c_cflag |= CS8;
            runtime_settings.c_lflag &= !(ECHO | ICANON | IEXTEN | ISIG);
            // returns each byte, or a 0-timeout.
            runtime_settings.c_cc[libc::VMIN] = 0;
            runtime_settings.c_cc[libc::VTIME] = 1; // 100 ms
            if libc::tcsetattr(stdin_fd, libc::TCSAFLUSH, &runtime_settings) == -1 {
                return Err("Failed to configure Midas command line");
            }
            Ok(TerminalConfiguration {
                original: Some(original_attributes),
                app_settings: Some(runtime_settings),
            })
        }
    }

    #[allow(dead_code)]
    pub fn reset(&self) {
        unsafe { libc::tcsetattr(0, libc::TCSAFLUSH, &self.original.unwrap()) };
    }
    #[allow(dead_code)]
    pub fn set(&self) {
        if self.app_settings.is_none() {
            println!("Midas Command Line not initialized");
        } else {
            unsafe {
                libc::tcsetattr(0, libc::TCSAFLUSH, &self.app_settings.unwrap());
            }
        }
    }
    #[allow(dead_code)]
    pub fn is_init(&self) -> bool {
        self.app_settings.is_some() && self.original.is_some()
    }
}
