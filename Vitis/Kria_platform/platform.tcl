# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct C:\GitHub\KRIA_Burst_Freertos\Vitis\Kria_platform\platform.tcl
# 
# OR launch xsct and run below command.
# source C:\GitHub\KRIA_Burst_Freertos\Vitis\Kria_platform\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {Kria_platform}\
-hw {C:\GitHub\KRIA_Burst_Freertos\Vivado\Products\kria_bd_wrapper.xsa}\
-proc {psu_cortexa53_0} -os {freertos10_xilinx} -arch {64-bit} -fsbl-target {psu_cortexa53_0} -out {C:/GitHub/KRIA_Burst_Freertos/Vitis}

platform write
platform generate -domains 
platform active {Kria_platform}
platform generate
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform active {Kria_platform}
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains freertos10_xilinx_domain 
platform clean
platform generate
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform clean
platform generate
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform clean
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate
