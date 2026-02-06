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
platform active {Kria_platform}
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
domain create -name {psu_cortexa53_0} -os {freertos} -proc {psu_cortexa53_0} -arch {64-bit} -display-name {psu_cortexa53_0} -desc {} -runtime {cpp}
platform generate -domains 
domain -report -json
platform write
bsp reload
domain remove psu_cortexa53_0
platform generate -domains 
platform write
domain create -name {psu_cortexa53_1} -os {freertos} -proc {psu_cortexa53_1} -arch {64-bit} -display-name {psu_cortexa53_1} -desc {} -runtime {cpp}
platform generate -domains 
domain -report -json
platform write
domain create -name {psu_cortexa53_2} -os {standalone} -proc {psu_cortexa53_0} -arch {64-bit} -display-name {psu_cortexa53_2} -desc {} -runtime {cpp}
platform generate -domains 
platform write
domain -report -json
domain remove psu_cortexa53_2
platform generate -domains 
platform write
domain create -name {psu_cortexa53_2} -os {freertos} -proc {psu_cortexa53_2} -arch {64-bit} -display-name {psu_cortexa53_2} -desc {} -runtime {cpp}
platform generate -domains 
domain -report -json
platform write
domain create -name {psu_cortexa53_3} -os {freertos} -proc {psu_cortexa53_3} -arch {64-bit} -display-name {psu_cortexa53_3} -desc {} -runtime {cpp}
platform generate -domains 
platform write
domain -report -json
platform generate
domain active {freertos10_xilinx_domain}
bsp reload
bsp config total_heap_size "262144"
bsp write
bsp reload
catch {bsp regenerate}
domain active {psu_cortexa53_1}
bsp reload
bsp config total_heap_size "262144"
bsp config tick_setup "true"
bsp write
bsp reload
catch {bsp regenerate}
domain active {psu_cortexa53_2}
bsp reload
bsp config total_heap_size "262144"
bsp write
bsp reload
catch {bsp regenerate}
domain active {psu_cortexa53_3}
bsp reload
bsp config total_heap_size "262144"
bsp write
bsp reload
catch {bsp regenerate}
platform generate -domains freertos10_xilinx_domain,psu_cortexa53_1,psu_cortexa53_2,psu_cortexa53_3 
domain active {freertos10_xilinx_domain}
bsp reload
bsp config total_heap_size "262144"
bsp reload
bsp config total_heap_size "262144"
bsp config clocking "false"
bsp config total_heap_size "262144"
bsp reload
platform generate -domains 
domain active {psu_cortexa53_1}
bsp reload
bsp config total_heap_size "262144"
bsp write
domain active {freertos10_xilinx_domain}
bsp reload
bsp write
domain active {psu_cortexa53_2}
bsp reload
bsp write
platform generate -domains 
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
domain active {zynqmp_fsbl}
domain active {freertos10_xilinx_domain}
bsp reload
domain active {psu_cortexa53_1}
bsp reload
bsp reload
domain active {freertos10_xilinx_domain}
bsp write
platform generate -domains 
platform generate
platform clean
platform generate
platform active {Kria_platform}
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform active {Kria_platform}
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
platform config -updatehw {C:/GitHub/KRIA_Burst_Freertos/Vivado/Products/kria_bd_wrapper.xsa}
platform generate -domains 
