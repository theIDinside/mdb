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

macro_rules! subjects {
    () => {
        concat!(tests_dir!(), "/executables")
    };
    ($e: expr) => {
        concat!(concat!(tests_dir!(), "/executables/"), $e)
    };
}

pub fn pre_test() {
    BuiltTestDebuggees.call_once(|| {
        let status = Command::new("make")
            .arg("all")
            .current_dir(tests_dir!())
            .spawn()
            .unwrap()
            .wait()
            .unwrap();
        assert!(status.success())
    });
}

// #[test]
pub fn exited() {
    let program_path = subjects!("helloworld");
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
            assert_eq!(status, WaitStatus::ExitedNormally(Pid(pid), 0));
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

#[test]
pub fn exit_with_exit_status_1() {
    use midas::target::Target;
    let program_path = subjects!("helloworld_exit_status_1");
    pre_test();
    let (target, waitstatus) =
        midas::target::linux::LinuxTarget::launch(std::path::Path::new(program_path)).unwrap();
    println!("Wait status after fork: {:?}", waitstatus);
    let status = target.continue_execution().unwrap();
    assert_eq!(status, WaitStatus::ExitedNormally(target.process_id(), 1));
}
