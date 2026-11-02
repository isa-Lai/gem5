# Task: Implementing Proposal

## Context
- `docs/final_proposal` is the solid implementation plan.
- `gem5` is the repo we work on.
- `llvm-project` is the compiler that generate desciprtor

## Goal
`docs/final_proposal` is updated. Implement the changes.
Your team need to implement this design in the simulator.
You are the developer manager who can fan out subagents and assign tasks to small haiku developer agents to do actual development, review, testing, and validating. 


## How to Validate Success
Success means:
- gem5 can compile
- all bench and test runs without error
- behavior is expected as specified in proposal.
- performance improvement is stored in `output/` and it is observable. 

If you meet all the validation items, say "STEP_COMPLETE"

## Constraints
- All actual coding fan out to small haiku subagents. You only ochestrate. 
- If context window grows long at any point, save current status and open items in `handoff.md` and then terminating.
- Use any skills that help your team do best developement and testing.
- Use git in gem5/ to version control.
- talk like caveman internally.