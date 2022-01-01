use linuxwrapper as nixwrap;
use midas::{self, target};
use nixwrap::WaitStatus;
use std::{panic, process::Command, sync::Once};

static BUILT_TEST_DEBUGGEES: Once = Once::new();

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

pub fn compile_subjects() {
    BUILT_TEST_DEBUGGEES.call_once(|| {
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

fn run_test<T>(test: T) -> ()
where
    T: FnOnce() -> () + panic::UnwindSafe,
{
    compile_subjects();
    let result = panic::catch_unwind(|| test());
    assert!(result.is_ok())
}

#[test]
pub fn exit_with_exit_status_1() {
    run_test(|| {
        use midas::target::Target;
        let program_path = subjects!("helloworld_exit_status_1");
        compile_subjects();
        let (target, waitstatus) = midas::target::linux::LinuxTarget::launch(
            &mut target::make_command(program_path, vec!["exit_with_exit_status_1"]).unwrap(),
        )
        .unwrap();
        assert_eq!(
            waitstatus,
            WaitStatus::Stopped(target.process_id(), nixwrap::signals::Signal::Trap)
        );
        let status = target.continue_execution().unwrap();
        assert_eq!(status, WaitStatus::ExitedNormally(target.process_id(), 1));
    })
}

#[test]
pub fn is_stopped_after_launch() {
    use midas::target::Target;

    run_test(|| {
        let program_path = subjects!("helloworld");
        let (target, waitstatus) = midas::target::linux::LinuxTarget::launch(
            &mut target::make_command(program_path, vec!["is_stopped_after_launch"]).unwrap(),
        )
        .unwrap();
        assert_eq!(
            waitstatus,
            WaitStatus::Stopped(target.process_id(), nixwrap::signals::Signal::Trap)
        );
    })
}

#[test]
pub fn exited() {
    run_test(|| {
        use midas::target::Target;
        let program_path = subjects!("helloworld");
        compile_subjects();
        let (target, waitstatus) = midas::target::linux::LinuxTarget::launch(
            &mut target::make_command(program_path, vec!["exited"]).unwrap(),
        )
        .unwrap();
        assert_eq!(
            waitstatus,
            WaitStatus::Stopped(target.process_id(), nixwrap::signals::Signal::Trap)
        );
        let status = target.continue_execution().unwrap();
        assert_eq!(status, WaitStatus::ExitedNormally(target.process_id(), 0));
    })
}
