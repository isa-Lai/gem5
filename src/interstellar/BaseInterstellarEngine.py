##################################################################
# InterStellar Centralized Engine GEM5 Simulation Object
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 2 March 2022
#
##################################################################


from m5.objects.ClockedObject import ClockedObject
from m5.params import *

from gem5.components.processors.cpu_types import CPUTypes


class TypeInterstellarEngine(ScopedEnum):
    """
    IPP: Meta ISA optimization policy is for intelligent page policy
    undefined: For future use

    """

    map = {
        "IPP": 0x1,
        "undefined": 0x2,
    }


class TypeProbeEnable(ScopedEnum):
    """
    Probes Available In Interstellar:
      Execute               -> 0. Not Required for Interstellar
      ToCommit              -> 1. Not Required for Interstellar
      Commit                -> 2. Not Required for Interstellar
      Miss                  -> 3. Not Required for Interstellar (Interstellar Engine already connected to the membus)
      va2pa                 -> Mandatory for Interstellar
      interstellar_csrwt    -> Mandatory for Interstellar

    None    (0) : Disable probing on:
               1. OoO pipeline stages  (Execute,ToCommit,Commit)
               2. L1DCache             (Miss)

    Execute  (1) : Enable Probe on Execue OoO.
    ToCommit (2) : Enable Probe on ToCommit OoO.
    Commit   (4) : Enable Probe on Commit OoO.
    L1Miss   (8) : Enable Probe on L1 DCache Miss
    To Mix :
              Execute_ToCommit           (3)
              Execute_Commit             (5)
              ToCommit_Commit            (6)
              Execute_ToCommit_Commit    (7)
              L1Miss_Execute             (9)
              L1Miss_ToCommit           (10)
              L1Miss_Execute_ToCommit   (11)
              L1Miss_Commit             (12)
              L1Miss_Execute_Commit     (13)
              L1Miss_ToCommit_Commit    (14)
              L1Miss_Execute_ToCommit_Commit    (15)
    All              (15): Enable Probe on all optional probes
    OoO_Pipeline     (7) : Enable Probe on OoO pipeline
    """

    map = {
        "None": 0x0,
        "Execute": 0x1,
        "ToCommit": 0x2,
        "Commit": 0x4,
        "L1Miss": 0x8,
        "Execute_ToCommit": 0x3,
        "Execute_Commit": 0x5,
        "ToCommit_Commit": 0x6,
        "Execute_ToCommit_Commit": 0x7,
        "OoO_Pipeline": 0x7,
        "L1Miss_Execute": 9,
        "L1Miss_ToCommit": 10,
        "L1Miss_Execute_ToCommit": 11,
        "L1Miss_Commit": 12,
        "L1Miss_Execute_Commit": 13,
        "L1Miss_ToCommit_Commit": 14,
        "L1Miss_Execute_ToCommit_Commit": 15,
        "All": 15,
    }


class TypeCPU(ScopedEnum):
    """
    Different options:
     {AtomicSimpleCPU,DerivO3CPU,MinorCPU,NonCachingSimpleCPU,O3CPU,TimingSimpleCPU,TraceCPU}]


    """

    map = {
        "AtomicSimpleCPU": 0x0,
        "DerivO3CPU": 0x1,
        "MinorCPU": 0x2,
        "NonCachingSimpleCPU": 0x3,
        "O3CPU": 0x4,
        "TimingSimpleCPU": 0x5,
        "TraceCPU": 0x6,
    }


class BaseInterstellarEngine(ClockedObject):
    type = "BaseInterstellarEngine"
    abstract = False
    cxx_header = "interstellar/baseInterstellarEngine.hh"
    cxx_class = "gem5::BaseInterstellarEngine"

    # Ports for memory requests and forwarding them
    llcSidePort = ResponsePort(
        "Upstream port closer to the CPU and/or device (Cache)"
    )
    memSidePort = RequestPort("Downstream port closer to memory")

    interstellar_csr_listerners_list = VectorParam.ProbeListenerObject(
        "listeners per each CPU's ISA"
    )
    cpu_probe_listeners_list = VectorParam.ProbeListenerObject(
        "listeners per each CPU"
    )
    l1DCaches_probe_listerners_list = VectorParam.ProbeListenerObject(
        "listeners per each CPU's L1 Data Cache"
    )
    dTLB_probe_listerners_list = VectorParam.ProbeListenerObject(
        "listeners per each CPU's MMU's Data TLB"
    )

    optimization_policy = Param.TypeInterstellarEngine("IPP / undefined")
    interstellar_probe_enable = Param.TypeProbeEnable(
        "None",
        " None (Diable OoO and L1DCache Probes) , Execute  , ToCommit, Commit , L1Miss , Mix using underscore , All.",
    )
    cpu_type = Param.TypeCPU(
        "AtomicSimpleCPU,DerivO3CPU,MinorCPU,NonCachingSimpleCPU,O3CPU,TimingSimpleCPU,TraceCPU"
    )

    isa_list = VectorParam.BaseISA("CPU's ISA List to probe")
    cpu_list = VectorParam.BaseCPU("CPU List to probe")
    l1_DCache_list = VectorParam.BaseCache("L1 Cache to probe")
    dTLB_list = VectorParam.BaseTLB("L1 Cache to probe")
    l2_cache = Param.BaseCache(NULL, "L2 Cache to probe")
