# Minimal barebones config to measure sim_insts (instruction count) only.
# Bypasses the fork's broken se.py cache topology. Atomic CPU => no caches,
# no engine, no Ruby => pure functional instruction count, which is what the
# golden-stats criterion (sim_insts must match exactly) needs.
import os
import sys

import m5
from m5.objects import *

binary = (
    sys.argv[1]
    if len(sys.argv) > 1
    else "/home/laiy24/interstelar/llvm-project/interstellar_test/complete_test.riscv"
)

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "atomic"
system.mem_ranges = [AddrRange("2GB")]

system.cpu = AtomicSimpleCPU()

system.membus = SystemXBar()
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.cpu.createInterruptController()

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

system.system_port = system.membus.cpu_side_ports

system.workload = SEWorkload.init_compatible(binary)
process = Process()
process.cmd = [binary]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()
print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))
