#!/usr/bin/env python3
"""
Comprehensive TLB Inverse Fix

This script applies the complete fix to remove the circular dependency
by moving TLB inverse population outside the if/else blocks.
"""

import re


def fix_tlb_inverse(filename):
    with open(filename) as f:
        content = f.read()

    # Define the old section that needs to be replaced
    old_section = r"""			Addr     reqVA = req->getVaddr\(\);
			uint64_t vpn = reqVA >> PageShift;

			Table1_Entry \*pAssocStream = descTable\.getStrEntByVARangeOpt\(reqVA\);

			string message_stream  = "";
			//Only Add it to TLB Inverse if it is a valid stream
			if \(pAssocStream != nullptr\)
			\{
				tlb_inverse\[pfn\] = vpn ;
				printf\("TLB-1\[%lx\]=%lx\\n",pfn,vpn\);
				//Note that the message indicates the first stream that has this PFN
				//As PFN can be shared between more than a stream .
				message_stream  =  "First found to Lie in "\+descNames\[pAssocStream->entryCSRData\.type\]\+" stream";
			\}
			else
			// If Not Direct Stream or Indirect Stream , It Can be pointer Chasing
			\{"""

    # Define the new section with the fix
    new_section = r"""			Addr     reqVA = req->getVaddr();
			uint64_t vpn = reqVA >> PageShift;

			// Check if this is a known stream
			Table1_Entry *pAssocStream = descTable.getStrEntByVARangeOpt(reqVA);

			if (pAssocStream != nullptr)
			{
				// This is a known direct/indirect stream - log it
				printf("TLB-1[%lx]=%lx (stream: %s)\n", pfn, vpn,
				       descNames[pAssocStream->entryCSRData.type].c_str());
			}
			else
			// If Not Direct Stream or Indirect Stream , It Can be pointer Chasing
			{"""

    # Replace the section
    new_content = re.sub(old_section, new_section, content, flags=re.MULTILINE)

    if new_content == content:
        print("❌ Pattern not found - trying alternative approach")
        return False

    # Now fix the DPRINTF call that references message_stream
    new_content = new_content.replace("message_stream.c_str()", '"\\0"')

    # Find the location after the if/else block ends and add the critical fix
    # Look for the end of the pointer chasing section
    marker = """				//this->descTable.setPtrChasePABySrchAll(req->getPaddr(),req->getVaddr());


			}"""

    replacement = """				//this->descTable.setPtrChasePABySrchAll(req->getPaddr(),req->getVaddr());


			}

			// CRITICAL FIX: Populate TLB inverse for ALL addresses, not just streams
			// This removes the circular dependency that prevented iHWP from working
			tlb_inverse[pfn] = vpn;
			printf("TLB-1[%lx]=%lx (FIXED: all addresses)\\n", pfn, vpn);"""

    new_content = new_content.replace(marker, replacement)

    with open(filename, "w") as f:
        f.write(new_content)

    print("✅ Successfully applied comprehensive TLB inverse fix")
    return True


if __name__ == "__main__":
    filename = (
        "/home/laiy24/interstelar/gem5/src/interstellar/RiscvMetaISAEngine.cc"
    )
    import sys

    if fix_tlb_inverse(filename):
        sys.exit(0)
    else:
        sys.exit(1)
