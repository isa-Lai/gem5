##################################################################
# Memory Stressor Simulation Object
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 15 July 2022
#
##################################################################

import sys

from m5.defines import buildEnv
from m5.objects.ClockDomain import *
from m5.objects.ClockedObject import ClockedObject
from m5.objects.XBar import L2XBar
from m5.params import *
from m5.proxy import *
from m5.SimObject import *
from m5.util.fdthelper import *

from gem5.components.processors.cpu_types import CPUTypes

# Memory stressor will communicate with an MMU
if buildEnv.get("USE_RISCV_ISA", False):
    from m5.objects.RiscvMMU import RiscvMMU as ArchMMU


class RiscvMemoryStressor(ClockedObject):
    type = "RiscvMemoryStressor"
    abstract = False
    cxx_header = "interstellar/RiscvMemoryStressor.hh"
    cxx_class = "gem5::RiscvMemoryStressor"

    system = Param.System(Parent.any, "system object")
    stressorId = Param.Int(-1, "Memory Stressor identifier")

    # following three paramters and the type of code to run
    # can be generalized in future to accept workload file instead
    dirStreamStride = Param.UInt64(
        64, "Stride length between each consecutive loads"
    )
    dirStreamMaxSize = Param.UInt64(2048, "Maximum Size of Load requests")
    dirStreamStartVA = Param.UInt64(0x14010, "Start VA of the memory stressor")
    loadReqInterArrivalDelay = Param.Int(
        16, "Inter-arrival delay between load requests - in system cycles-"
    )
    stressorWorkLoad = Param.String("", "Stressor Workload file")

    mmu = Param.BaseMMU(ArchMMU(), "CPU memory management unit")
    isa = VectorParam.BaseISA([], "ISA instance")
    numThreads = Param.Unsigned(1, "number of HW thread contexts")
    # Ports for memory requests/responses
    dcache_port = RequestPort("Data Port")
    _cached_ports = ["dcache_port"]

    def connectCachedPorts(self, in_ports):
        for p in self._cached_ports:
            exec("self.%s = in_ports" % p)

    # For now , There is no UncachedPorts , But just to follow same interfaces as CPU
    def connectAllPorts(self, cached_in):
        self.connectCachedPorts(cached_in)

    def connectBus(self, bus):
        self.connectAllPorts(bus.cpu_side_ports)

    def addPrivateSplitL1Caches(self, dc, dwc=None):
        self.dcache = dc
        self.dcache_port = dc.cpu_side
        self._cached_ports = ["dcache.mem_side"]
        if dwc:
            self.dtb_walker_cache = dwc
            self.mmu.connectWalkerPorts(
                None, dwc.cpu_side
            )  # No Instruction TLB needed
            self._cached_ports += ["dtb_walker_cache.mem_side"]
        else:
            self._cached_ports += ArchMMU.walkerPorts()

    def addTwoLevelCacheHierarchy(self, dc, l2c, dwc=None, xbar=None):
        self.addPrivateSplitL1Caches(dc, dwc)
        self.toL2Bus = xbar if xbar else L2XBar()
        self.connectCachedPorts(self.toL2Bus.cpu_side_ports)
        self.l2cache = l2c
        self.toL2Bus.mem_side_ports = self.l2cache.cpu_side
        self._cached_ports = ["l2cache.mem_side"]

    def createThreads(self):
        if buildEnv.get("USE_RISCV_ISA", False):
            from m5.objects.RiscvInterrupts import (
                RiscvInterrupts as ArchInterrupts,
            )
            from m5.objects.RiscvISA import RiscvISA as ArchISA
            from m5.objects.RiscvMMU import RiscvMMU as ArchMMU

        self.isa = list([ArchISA() for i in range(self.numThreads)])
