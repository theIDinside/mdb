pub enum StepRequest {
    // Instructions always step in
    Instruction(usize),
    // source lines always step over
    NextSourceLine { count: usize, step_over: bool },
}
impl StepRequest {
    pub(crate) fn description(&self) -> &'static str {
        match self {
            StepRequest::Instruction(_) => "Step N machine instructions",
            StepRequest::NextSourceLine { .. } => "Step N source code lines",
        }
    }
}
