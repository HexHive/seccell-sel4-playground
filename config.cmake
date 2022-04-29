cmake_minimum_required(VERSION 3.7.2)

set(configure_string "")

config_choice(
    EvaluationType
    EVAL_TYPE
    "Select the evaluation type. \
    uncomp -> Classic seL4, no compartmentalization. \
    comp -> SecureCells, copy data between compartments. \
    oszcopy -> seL4 processes/threads with shared memory (zero-copy). \
    sczcopy -> SecureCells with permission transfers (zero-copy)."
    "uncomp;EvaluationTypeUncomp;EVAL_TYPE_UNCOMP;KernelArchRiscV"
    "comp;EvaluationTypeComp;EVAL_TYPE_COMP;KernelArchRiscV AND KernelSecCell"
    "oszcopy;EvaluationTypeOSZeroCopy;EVAL_TYPE_OS_ZCOPY;KernelArchRiscV AND NOT KernelSecCell"
    "sczcopy;EvaluationTypeSCZeroCopy;EVAL_TYPE_SC_ZCOPY;KernelArchRiscV AND KernelSecCell"
)

add_config_library(seL4-playground "${configure_string}")
