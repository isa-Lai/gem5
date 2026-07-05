##################################################################
# Configure System by adding MetaISA Centralized Engine SimObject
#
# Author: Abdelrhman Mohamed Abotaleb (aabotaleb)
# Email : abotalea@mcmaster.ca
#
# Date  : 7 July 2022
#
##################################################################


import m5
from m5.objects import *


def customizeCPU(CPUClass, cpu_list):
    if issubclass(CPUClass, m5.objects.DerivO3CPU):
        for cpu in cpu_list:
            # Make the number of entries in the ROB, LQ and SQ very
            # large so that there are no stalls due to resource
            # limitation as such stalls will get captured in the trace
            # as compute delay. For replay, ROB, LQ and SQ sizes are
            # modelled in the Trace CPU.
            ##cpu.numROBEntries = 512;
            ##cpu.LQEntries = 128; #default is 32
            ##cpu.SQEntries = 128; #default is 32
            ##cpu.numIQEntries = 256 #default is 64
            # cpu.LQEntries = 64
            # cpu.SQEntries = 64
            # ALlow for more fetch  - issue - rename - squashes
            # cpu.commitWidth=12
            # cpu.decodeWidth=12
            # cpu.dispatchWidth=12
            # cpu.fetchWidth=12
            # cpu.issueWidth=12
            # cpu.renameWidth=12
            # cpu.squashWidth=12
            # cpu.wbWidth=12
            # cpu.fetchQueueSize=64
            renameWidth = 1

    if issubclass(CPUClass, m5.objects.MinorCPU):
        for cpu in cpu_list:
            cpu.executeInputWidth = 4  # Param.Unsigned(2,"Width (in instructions) of input to Execute")
            # executeCycleInput = False #Param.Bool(True,"Allow Execute to use instructions from more than one input cycle each cycle")
            cpu.executeIssueLimit = 4  # Param.Unsigned(2,"Number of issuable instructions in Execute each cycle")
            # executeMemoryIssueLimit = Param.Unsigned(1, "Number of issuable memory instructions in Execute each cycle")
            cpu.executeCommitLimit = 4  # Param.Unsigned(2,  "Number of committable instructions in Execute each cycle")
            # executeMemoryCommitLimit = Param.Unsigned(1, "Number of committable memory references in Execute each cycle")
            cpu.executeInputBufferSize = 8  # Param.Unsigned(7, "Size of input buffer to Execute in cycles-worth of insts.")
            # executeMemoryWidth = Param.Unsigned(0, "Width in bytes of the data memory interface. (0 mean use  the system cacheLineSize)")
            cpu.executeMaxAccessesInMemory = 4  # Param.Unsigned(2, "max concurrent accesses to memory system from the dcache port")
            cpu.executeLSQMaxStoreBufferStoresPerCycle = 4  # Param.Unsigned(2, "max stores  the store buffer can issue / cycle")
            # executeLSQRequestsQueueSize = Param.Unsigned(1, "Size of LSQ requests queue (address translation queue)")
            cpu.executeLSQTransfersQueueSize = 4  # Param.Unsigned(2, "Size of LSQ transfers queue (memory transaction queue)")
            cpu.executeLSQStoreBufferSize = (
                6  # Param.Unsigned(5, "Size of LSQ store buffer")
            )
            # executeBranchDelay = Param.Cycles(1, "Delay from Execute deciding to branch and Fetch1 reacting (1 means next cycle)")
