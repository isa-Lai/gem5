##################################################################
# Configure System by adding MetaISA Centralized Engine SimObject
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 14 March 2022
#
##################################################################


from common.Caches import *

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.objects import ProbeListenerObject


def config_meta_isa_engine(options, system):
    if options.meta_isa_type == 'None':
        return

    # v25.1 dropped TARGET_ISA from buildEnv; use the per-ISA compile flag.
    if buildEnv.get("USE_RISCV_ISA"):

        # TODO In future, with so many policies to be applied , inherit custom engine
        #      from RiscvMetaISAEngine and assign to the system a one based on meta_isa_type
        #      i.e. check : if options.meta_isa_type == 'IPP':

        # Create list of listeners , each listener is associated with a certain CPU
        # 0- Probes inside CSR registers
        # There are numThreads thread (isa object) per each num_cpus cpu:
        metaisa_csr_listerners_vec = []
        isa_vec = []
        dtlb_vec = []
        cpu_probe_listerners_vec = []
        l1DCaches_probe_listerners_vec = []
        dTLB_probe_listerners_vec = []
        stressors_list = []
        stressors_probe_listerners_list = []

        # Update MetaISAEngineConfig to work with the memory stressor
        if options.num_mem_stressors > 0:
            cpuList = [NULL]
            l1DCache_vec = [system.RiscvMemoryStressor.dcache]
            stressors_list = [system.RiscvMemoryStressor]
            system.RiscvMemoryStressor.probe_listener = ProbeListenerObject(
                manager=(system.RiscvMemoryStressor)
            )
            stressors_probe_listerners_list = [
                system.RiscvMemoryStressor.probe_listener
            ]
        if options.num_cpus > 0:
            cpuList = system.cpu

            for i in range(options.num_cpus):
                for j in range(system.cpu[i].numThreads):
                    # print( (system.cpu[i].isa[0] ).__class__.__name__)
                    system.cpu[i].isa[j].probe_listener = ProbeListenerObject(
                        manager=(system.cpu[i].isa[j])
                    )
                    metaisa_csr_listerners_vec.append(
                        system.cpu[i].isa[j].probe_listener
                    )
                    isa_vec.append(system.cpu[i].isa[j])
            # 1- Probes Inside the processor
            for i in range(options.num_cpus):
                system.cpu[i].probe_listener = ProbeListenerObject(
                    manager=system.cpu[i]
                )
            cpu_probe_listerners_vec = [
                system.cpu[i].probe_listener for i in range(options.num_cpus)
            ]
            # 2- Probes inside cache associated with each processor
            for i in range(options.num_cpus):
                system.cpu[i].dcache.probe_listener = ProbeListenerObject(
                    manager=system.cpu[i].dcache
                )
            l1DCaches_probe_listerners_vec = [
                system.cpu[i].dcache.probe_listener
                for i in range(options.num_cpus)
            ]
            l1DCache_vec = [
                system.cpu[i].dcache for i in range(options.num_cpus)
            ]
            # 3- Probes inside MMU.TLB
            for i in range(options.num_cpus):
                system.cpu[i].mmu.dtb.probe_listener = ProbeListenerObject(
                    manager=system.cpu[i].mmu.dtb
                )
            dTLB_probe_listerners_vec = [
                system.cpu[i].mmu.dtb.probe_listener
                for i in range(options.num_cpus)
            ]
            dtlb_vec = [system.cpu[i].mmu.dtb for i in range(options.num_cpus)]

        # print(probe_listerners_vec[0].manager)
        # Create MetaISA Engine and make it using the same clock as the CPU
        # The manager will initialize the ProbeListenerObject manaer to be the system cpu.
        systemL2 = NULL
        # NullSimObject() L2Cache() ;   #ALlow for system without L2
        if hasattr(system, "l2"):
            systemL2 = system.l2

        system.metaISAEngine = RiscvMetaISAEngine(
            clk_domain=system.cpu_clk_domain,
            optimization_policy=options.meta_isa_type,
            cpu_type=options.cpu_type,
            interstellar_probe_enable=options.meta_isa_probe_enable,
            # Pointer to simObjects needed inside MetaISA
            isa_list=isa_vec,
            cpu_list=cpuList,
            l1_DCache_list=l1DCache_vec,
            l2_cache=NULL,
            dTLB_list=dtlb_vec,
            # Probe Listeners
            interstellar_csr_listerners_list=metaisa_csr_listerners_vec,
            cpu_probe_listeners_list=cpu_probe_listerners_vec,
            l1DCaches_probe_listerners_list=l1DCaches_probe_listerners_vec,
            dTLB_probe_listerners_list=dTLB_probe_listerners_vec,
            stressors_list=stressors_list,
            stressors_probe_listerners_list=stressors_probe_listerners_list,
        )

        system.metaisa_membus = IOXBar()
        system.metaISAEngine.memSidePort = system.metaisa_membus.cpu_side_ports
        system.metaISAEngine.llcSidePort = system.membus.mem_side_ports

        # Bind interstellar_nucleus of any InterStellarPrefetcher instances to system.metaISAEngine
        if hasattr(system, 'l2') and system.l2 and hasattr(system.l2, 'prefetcher') and system.l2.prefetcher != NULL:
            if system.l2.prefetcher.type == 'InterStellarPrefetcher':
                system.l2.prefetcher.interstellar_nucleus = system.metaISAEngine

        for i in range(options.num_cpus):
            if hasattr(system.cpu[i], 'dcache') and system.cpu[i].dcache and hasattr(system.cpu[i].dcache, 'prefetcher') and system.cpu[i].dcache.prefetcher != NULL:
                if system.cpu[i].dcache.prefetcher.type == 'InterStellarPrefetcher':
                    system.cpu[i].dcache.prefetcher.interstellar_nucleus = system.metaISAEngine
