# Experiment Loop
Each loop has 4 stages. Each stages are using different team and start different fresh session. They will say "STEP_COMPLETE" when they done.
1. `PROMPT_dev.md` : develope team implement the design.
2. `PROMPT_exp.md` : experiment team run experiment and generate result. 
3. `PROMPT_result.md` : result team analyze and evaluate the gap. 
4. `PROMPT_board.md` : research team refine the design. (If still gap then go to 1)

Talk like caveman internally. Use those md as your template. Loop until no gap found in stage 4.
If the experiment proposal did not change, say "EXPERIMENT_COMPLETE" and exit.