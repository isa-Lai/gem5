# Task: Refine research

## Context
- `docs/final_proposal` is the implementation plan. Refine this.
- `docs/proposal.md` is the original idea
- `docs/papers` has the related papers.
- `gem5` is the repo we work on.
- `llvm-project` is the compiler that generate desciprtor
- `output` has the current design experiment result.


## Goal
Refine the current design based on experiment result in `output`.
Your will form a research board team with different profession background subagents to discuss, find the gap, and iteratively improve the proposal. At least 1/3 of the board will always critique the proposal hard trying to find the gap in new proposal. 
Every iterations refresh the board context (new subagents).
You only summarize the state and log them into `board/`. 
In the end once the board agrees and you think the proposal is solid, you save the final proposal into `docs/final_proposal/`.

## To do
1. Validate if current result can be a strong candidate for high impact journal/conderence like ISCA/MICRO/ASPLOS/HPCA.
2. Identify the gap and weakness. Also evaluate the gap in `critic.md`.
3. Propose better solutions or change the application to more domain specific workload (ie, AI workload)
4. Redesign the experiment steps, how to evaluate and bench/workload to measure.

## How to Validate Success
Success means:
- All board members agree that current plan is feasible, gap is solved, and can contribute high impact value to the computer architecture research. If any gap still exist, it can be either negligibal or out of scope only.
- docs/final_proposal/* exist and contains all the details that a researcher can follow without asking questions. 
- issue in critic.md are either not worth to fix or all addressed.

If you meet all the validation items, say "STEP_COMPLETE"

## Constraints
- No Code. Only discussion.
- All actual decisions fan out to small haiku/glm subagents. You only ochestrate. 
- If context window grows long at any point, save current status and open items in `handoff.md` and then terminating.
- Use any skills that help your team do best scientific research.