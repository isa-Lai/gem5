ramulator_Config_path="/home/abotaleb/arena/Gem5/gem5/ext/ramulator/Ramulator/configs"
ramulator_Traces_path="/home/abotaleb/arena/Gem5/gem5/ext/ramulator/Ramulator/metaisa_traces"

conf_op="DDR4-config-OP.cfg"
conf_ipp="DDR4-config-IPP.cfg"
options=" --mode=dram" # either "cpu" or "dram"
options2=" --mode=cpu" # either "cpu" or "dram"
traceFNameOp="trace_1_op" 
traceFNameIPP="trace_1_cpu_ipp" 


conf_file_op=$ramulator_Config_path"/"$conf_op
conf_file_ipp=$ramulator_Config_path"/"$conf_ipp
traceFile_op=$ramulator_Traces_path"/"$traceFNameOp
traceFile_ipp=$ramulator_Traces_path"/"$traceFNameIPP

./ramulator $conf_file_op   $options $traceFile_op   >  open_trace.txt 2>&1
./ramulator $conf_file_ipp  $options2 $traceFile_ipp >  ipp_trace.txt  2>&1

