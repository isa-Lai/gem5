##################################################################
# MetaISA Centralized Engine GEM5 Simulation Object
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 2 March 2022
#
##################################################################

import sys

from m5.objects import *
from m5.objects.BaseInterstellarEngine import BaseInterstellarEngine
from m5.objects.ClockedObject import ClockedObject
from m5.objects.RiscvDecoder import RiscvDecoder
from m5.objects.RiscvInterrupts import RiscvInterrupts
from m5.objects.RiscvISA import RiscvISA
from m5.objects.RiscvMMU import RiscvMMU
from m5.params import *
from m5.SimObject import *


class RiscvMetaISAEngine(BaseInterstellarEngine):
    type = "RiscvMetaISAEngine"
    abstract = False
    cxx_header = "interstellar/RiscvMetaISAEngine.hh"
    cxx_class = "gem5::RiscvISA::RiscvMetaISAEngine"

    BPQSize = Param.UInt16(
        8192, "Blocked Packet Queue Size with Default Value = 8192"
    )

    stressors_probe_listerners_list = VectorParam.ProbeListenerObject(
        "listeners per each Stressor"
    )
    stressors_list = VectorParam.RiscvMemoryStressor("Stressors List to probe")
