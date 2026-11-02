# Task: Evaluate Result in Critic

## Context
- `docs/final_proposal` is the solid implementation plan.
- `gem5` is the repo we work on.
- `llvm-project` is the compiler that generate desciprtor
- `output` has the current design experiment result.


## Goal
Evaluate the current design based on experiment result in `output`.
Your will form a research board team with different profession background subagents to discuss, find the gap in current experiment result. At least 1/3 of the board will always critique the proposal hard trying to find the gap in new proposal. 
Every iterations refresh the board context (new subagents).
You only summarize the state and log them into `eval/`. 
In the end put severe issue in `critic.md`.

## To do
1. Validate if current result can be a strong candidate for high impact journal/conderence like ISCA/MICRO/ASPLOS/HPCA.
2. Identify the gap and weakness, or wrong experiment design.
3. Put your evaluation into critic.md and remove already addressed issue.

## How to Validate Success
Success means:
- All board members agree that issue in `critic.md` is severer and have value to fix.

If you meet all the validation items, say "STEP_COMPLETE"

## Constraints
- No Code. Only discussion.
- All actual decisions fan out to small haiku/glm subagents. You only ochestrate. 
- If context window grows long at any point, save current status and open items in `handoff.md` and then terminating.
- Use any skills that help your team do best scientific research.