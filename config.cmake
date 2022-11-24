cmake_minimum_required(VERSION 3.7.2)

set(configure_string "")

config_choice(
    EvaluationType
    EVAL_TYPE
    "Select the evaluation type. \
    uncomp -> Classic seL4, no compartmentalization. \
    comp -> Compartmentalized with SecureCells"
    "uncomp;EvaluationTypeUncomp;EVAL_TYPE_UNCOMP;KernelArchRiscV"
    "comp;EvaluationTypeComp;EVAL_TYPE_COMP;KernelArchRiscV AND KernelSecCell"
)

config_option(
    PrintAsCSV PRINT_CSV "Print statistics in CSV format instead of nicely human-readable."
    DEFAULT OFF
)

add_config_library(seL4-playground "${configure_string}")