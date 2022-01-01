use linuxwrapper as nixwrap;
use midas;
use midas::debugger::Debugger;
use nixwrap::{Pid, WaitStatus};
use std::{process::Command, sync::Once};

static BuiltTestDebuggees: Once = Once::new();

macro_rules! tests_dir {
    () => {
        concat!(env!("CARGO_MANIFEST_DIR"), "/tests/subjects")
    };
}

pub fn pre_test() {
    BuiltTestDebuggees.call_once(|| {
        let status = Command::new("make")
            .current_dir(tests_dir!())
            .spawn()
            .unwrap()
            .wait()
            .unwrap();
        assert!(status.success())
    });
}

#[test]
pub fn exited() {
    let program_path = concat!(tests_dir!(), "/helloworld");
    pre_test();
    let fork = nixwrap::fork().unwrap();
    match fork {
        nixwrap::Fork::Parent(pid) => {
            let mut debugger = Debugger::new(program_path.to_owned(), Pid(pid));
            println!(
                "Debugging debuggee with pid {} and path: {}",
                pid, program_path
            );
            let status = debugger.continue_execution().unwrap();
            println!("continue execution...");

            assert_eq!(status, WaitStatus::Stopped(Pid(pid)));
        }
        nixwrap::Fork::Child => {
            match nixwrap::begin_trace_target(std::path::Path::new(program_path)) {
                Ok(()) => {
                    assert_eq!(true, true);
                }
                Err(err) => return Err(err).unwrap(),
            }
        }
    }
}
