#!/usr/bin/env python3
"""
Fix TLB Inverse Circular Dependency

This script modifies the RiscvMetaISAEngine.cc file to populate the TLB
inverse mapping for ALL addresses, not just stream addresses. This removes
the circular dependency that has prevented iHWP from ever activating.
"""

import sys


def fix_tlb_inverse(filename):
    with open(filename) as f:
        lines = f.readlines()

    modified = False
    new_lines = []
    i = 0

    while i < len(lines):
        line = lines[i]

        # Find the critical section where we need to make changes
        if i == 844 and lines[i].strip() == "":
            # Check if we're at the right place
            if (
                i + 1 < len(lines)
                and "Table1_Entry *pAssocStream" in lines[i + 1]
            ):
                # Replace the entire section from lines 845-856
                new_lines.append("\n")
                new_lines.append(
                    "\t\t\t\t// FIXED: Populate TLB inverse for ALL addresses, not just stream addresses\n"
                )
                new_lines.append(
                    "\t\t\t\t// This removes the circular dependency that prevented iHWP from working\n"
                )
                new_lines.append("\t\t\t\ttlb_inverse[pfn] = vpn;\n")
                new_lines.append("\n")
                new_lines.append(
                    "\t\t\t\t// Optional logging to track stream vs non-stream addresses\n"
                )
                new_lines.append(
                    "\t\t\t\tTable1_Entry *pAssocStream = descTable.getStrEntByVARangeOpt(reqVA);\n"
                )
                new_lines.append("\t\t\t\tif (pAssocStream != nullptr)\n")
                new_lines.append("\t\t\t\t{\n")
                new_lines.append(
                    '\t\t\t\t\tprintf("TLB-1[%lx]=%lx (stream)\\n",pfn,vpn);\n'
                )
                new_lines.append("\t\t\t\t}\n")

                # Skip the old lines 845-856
                i += 12  # Skip lines 845-856 (12 lines)
                modified = True
                continue

        # Also need to fix the pointer chasing section at line ~883
        if i >= 880 and i < 900 and "tlb_inverse[pfn] = vpn;" in line:
            # Check if this is inside the pointer chasing section
            if "else" in lines[i - 3 : i]:  # Look back for else clause
                # This should be the old pointer chasing tlb_inverse insert
                # Replace it to just have printf
                new_lines.append(
                    '\t\t\t\t\t\tprintf("TLB-1[%lx]=%lx (pointer chasing)\\n",pfn,vpn);\n'
                )
                i += 1  # Skip the next line which should be the duplicate
                modified = True
                continue

        new_lines.append(line)
        i += 1

    if modified:
        with open(filename, "w") as f:
            f.writelines(new_lines)
        print("✅ Successfully applied TLB inverse fix")
        return 0
    else:
        print("❌ Could not find the expected code section to modify")
        return 1


if __name__ == "__main__":
    filename = (
        "/home/laiy24/interstelar/gem5/src/interstellar/RiscvMetaISAEngine.cc"
    )
    sys.exit(fix_tlb_inverse(filename))
