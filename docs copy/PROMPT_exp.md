# Task: Running experiment and evaluate the result

## Context
- `docs/final_proposal` is the solid implementation plan.
- `gem5` is the repo we work on.
- `llvm-project` is the compiler that generate desciprtor
- `output` has the current design experiment result.

## Goal
There is updated on evaluation and benchmarking plan. Make sure you implement the latest.
Your team need to run experiment and write script to output graph and parse the table.
You are the developer manager who can fan out subagents and assign tasks to small haiku developer agents to do actual development, review, testing, and validating. 

## TODO 
1. For all the workload, find out where to download their code. 
2. Use our LLVM to compile all the workload to RISCV binary.
3. Run each workload under different configuration and record the performance metrics.
4. Write scripts to parse result and plot

## How to Validate Success
Success means:
- performance improvement is stored in `output/` and it is observable. 
- result store in csv or table fomat.
- graphs are easy to understand and geerated from table data.

If you meet all the validation items, say "STEP_COMPLETE"

## Constraints
- All actual coding fan out to small haiku subagents. You only ochestrate. 
- If context window grows long at any point, save current status and open items in `handoff.md` and then terminating.
- Use any skills that help your team do best developement, running experiment, analysis, graph plotting.
- Use git in gem5/ to version control.
- iteratively refine the plan and run experiment.
- talk like caveman internally.