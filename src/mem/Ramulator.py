# -*- mode:python -*-
from m5.params import *
from m5.objects.AbstractMemory import *

# A wrapper for Ramulator multi-channel memory controller
class Ramulator(AbstractMemory):
    type = 'Ramulator'
    cxx_header = "mem/ramulator.hh"

    # A single port for now
    port = ResponsePort("Slave port")

    config_file  = Param.String  ("", "configuration file"                       )
    cmdTracePath = Param.String  ("", "command trace path"                       ) 
    num_cpus     = Param.Unsigned(1, "Number of cpu"                             )
    is_ideal_mem = Param.Bool    (False, "Emulate ideal DRAM to generate traces" )
    # gagan
    real_warm_up = Param.UInt64(100, "specify the real warm up time")
    output_dir = Param.String("", "Ramulator trace output")
