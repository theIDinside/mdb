use linuxwrapper as nixwrap;
use midas::{
    self,
    software_breakpoint::{self, BreakpointRequest},
    target,
    types::Address,
};
use nixwrap::WaitStatus;
use std::{panic, process::Command, sync::Once, time::Duration};

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
        let (mut target, waitstatus) = midas::target::linux::LinuxTarget::launch(
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

// todo(simon): this should be a success-test not a should fail; but for now the functionality is not there yet.
#[test]
pub fn set_bp_at_main_and_stops() {
    use midas::target::Target;
    run_test(|| {
        let main_address_of_helloworld = 0x401130;
        let before_print = 0x401162;
        let program_path = subjects!("helloworld");
        let (mut target, waitstatus) = midas::target::linux::LinuxTarget::launch(
            &mut target::make_command(program_path, vec!["is_stopped_after_launch"]).unwrap(),
        )
        .unwrap();
        let regs = nixwrap::ptrace::get_regs(target.process_id());
        target
            .set_breakpoint(BreakpointRequest::Address(Address(
                main_address_of_helloworld,
            )))
            .unwrap();
        target
            .set_breakpoint(BreakpointRequest::Address(Address(before_print)))
            .unwrap();

        let waitstatus = target
            .continue_execution()
            .expect("failed to continue execution");
        match waitstatus {
            WaitStatus::Stopped(pid, signal) => {
                assert_eq!(signal, nixwrap::signals::Signal::Trap);
                let regs = nixwrap::ptrace::get_regs(pid);
                assert_eq!(regs.rip - 1, main_address_of_helloworld as _);
                target
                    .continue_execution()
                    .expect("failed to continue execution");
                let regs = nixwrap::ptrace::get_regs(target.process_id());
                assert_eq!(regs.rip - 1, before_print as _);
                target
                    .continue_execution()
                    .expect("failed to continue execution");
                let regs = nixwrap::ptrace::get_regs(target.process_id());
                // process should have exited at this point, thus, all registers should be = 0
                assert_eq!(
                    regs,
                    nixwrap::ptrace::UserRegisters::from(nixwrap::ptrace::init_user_regs())
                );
            }
            _ => assert!(
                false,
                "Wrong wait status encountered after hitting breakpoint"
            ),
        }
    })
}

#[test]
pub fn exited() {
    run_test(|| {
        use midas::target::Target;
        let program_path = subjects!("helloworld");
        compile_subjects();
        let (mut target, waitstatus) =
            midas::target::linux::LinuxTarget::launch(&mut target::make_command(program_path, vec!["exited"]).unwrap())
                .unwrap();
        assert_eq!(
            waitstatus,
            WaitStatus::Stopped(target.process_id(), nixwrap::signals::Signal::Trap)
        );
        let status = target.continue_execution().unwrap();
        assert_eq!(status, WaitStatus::ExitedNormally(target.process_id(), 0));
    })
}
