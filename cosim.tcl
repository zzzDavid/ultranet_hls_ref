open_project ultranet_cosim
set_top ultranet_cosim_wrapper
add_files ultranet.cpp
add_files ultranet_cosim_wrapper.cpp
add_files stream_tools.h
add_files sliding_window_unit.h
add_files pool2d.h
add_files param.h
add_files matrix_vector_unit.h
add_files function.h
add_files conv2d.h
add_files config.h
add_files bn_qrelu2d.h
add_files -tb cosim_tb.cpp
open_solution "solution1"
set_part {xczu3eg-sbva484-1-i}
create_clock -period 10 -name default
if {[lindex $argv 2] == "presim"} {
    csim_design -argv presim
} elseif {[lindex $argv 2] == "postsim"} {
    csynth_design
    cosim_design -argv postsim
}
exit
