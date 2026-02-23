# RL-Based Analog Circuit Sizing (AutoCkt)
The AutoCkt Solver is a reinforcement learning (RL) framework designed to automate the sizing of transistor parameters in analog circuits. Leveraging the AutoCkt environment and PPO (Proximal Policy Optimization) algorithms, the system iteratively searches for optimal design variables—such as transistor widths and compensation capacitance—to satisfy a diverse set of performance specifications including gain, phase margin, and power consumption.

## Problem Formulation
Given a two-stage operational amplifier (op-amp) topology and a set of target specifications (specs), the goal is to find a set of discrete parameter indices that, when simulated in NGSpice, result in circuit performance meeting or exceeding all criteria. The solver must navigate a complex, multi-dimensional search space while optimizing for: (1) 100% success rate across various target spec files, and (2) minimizing the number of simulation trials (iterations). The output provides the final parameter set and a verified SPICE netlist for the design.

## Features
- **Standalone Loading**: Restores weights via pickle and `__setstate__` without requiring `.tune_metadata` and `params.json`.
- **Margin Integration**: Applies safety margins to target specs to ensure 100% verification success in NGSpice.
- **Interactive Visualization**: Dual Y-axis dashboards for evaluating training trends and checkpoint quality.
- **Search Recovery**: Automatic reset mechanism that escapes local optima by restarting the search after a trial limit.

## Processing Pipeline
1. **Stage 1 Model Training (Ref [Installation](Installation.md))**:
    1. Environment Registration: Define the TwoStageAmp gym environment and register it using Ray/RLlib for reinforcement learning.
    2. PPO Training: Execute training sessions using Proximal Policy Optimization (PPO) to learn optimal sizing actions across a multi-dimensional parameter space.
    3. Quality Monitoring: Track episode_reward_mean and episode_len_mean via progress.csv to monitor model convergence and stability.
    4. Model Verification: Validate selected checkpoints against target specifications to confirm the success rate (e.g., 85/100) and ensure performance consistency.
2. **Stage 2 Inference & Sizing**:
    1. Robust Setup: Restore weights independently and apply safety margins to target specs to ensure 100% compliance.
    2. Iterative Inference: Executes a feedback-driven search loop with an automated reset mechanism to ensure convergence within trial limits.
    3. Verification and Export: Generate the final netlist and execute ngspice_checker.py to confirm definitive spec success.

## Input / Output Format
### Input
- **spec.json**
```
{
	"gain_min": 200~400,
	"ibias_max":  0.0001~0.01,
	"phm_min": 60.0,
	"ugbw_min":  1.0e6~2.5e7
}
```

**Example**
```
{
	"gain_min": 300,
	"ibias_max": 0.006,
	"phm_min": 60.0,
	"ugbw_min": 20000000.0
}
```

- **model**: Checkpoint generated during training.

### Output
**final_design.cir (follow the spice template)**  
Fill in parameters
```
two_stage_amp test

* Two stage OPAMP
.include /tmp/ngspice_model/45nm_bulk.txt

*********** TODO ***********
.param wp1= lp1=90n mp1=
.param wn1= ln1=90n mn1=
.param wn3= ln3=90n mn3=
.param wp3= lp3=90n mp3=
.param wn4= ln4=90n mn4=
.param wn5= ln5=90n mn5=
.param cc=
****************************

.param ibias=30u
.param cload=10p
.param vcm=0.6
...
```

**Example**  
Only show the TODO block, width should be 0.5u, can be found in the `eval_engines/ngspice/ngspice_inputs/netlist/two_stage_opamp.cir` or `ckt_da/designs_two_stage_opamp/*.cir`
```
*********** TODO ***********
.param wp1=0.5u lp1=90n mp1=10
.param wn1=0.5u ln1=90n mn1=22
.param wn3=0.5u ln3=90n mn3=17
.param wp3=0.5u lp3=90n mp3=89
.param wn4=0.5u ln4=90n mn4=6
.param wn5=0.5u ln5=90n mn5=62
.param cc=4e-12
****************************
```

## Environment

|   Operating System   |      Python Version      |     Usage Scenarios      |
|----------------------|--------------------------|--------------------------|
| Ubuntu 22.04         | python 3.5.6 (conda)     | Execution & Verification |
| Rocky Linux 8.10     | python 3.6.8             | Verification only        |



## Directory Structure
```
$HOME/
  ├── AutoCkt
  │   ├── ...                        // Other folders
  │   ├── ckt_da/                    // Generated circuits
  │   ├── sizing.py
  │   └── ngspice_checker.py
  |
  ├── ray_results
  │   └── train_45nm_ngspice
  │       └── PPO_TwoStageAmp...
  │           ├── progress.csv       // checkpoint status
  │           └── checkpoint_XXX
  │               └── checkpoint-XXX // model
  |
  ├── episode.py                     // Python script to visualize checkpoint status
  ├── PA4_demo.sh
  |
  ├── 2025_CAD_HW4.zip               // TA-provided resources (testcases, tutorial, templates, etc.)
  ├── Installation.md
  └── README.md
```

## Usage Guide
### How to execute
Run the program with
```
conda activate autockt
cd Autockt
export PATH=$HOME/ngspice-27/opt/bin:/$PATH
python sizing.py --model <model> --spec <spec>.json
```

### How to verify
```
conda activate autockt
cd Autockt

// Ver 1., in your own environment
// Move ngspice_checker.py and final_design.cir into AutoCkt
// Change final_design.cir .include path to ~/AutoCkt/eval_engines/ngspice/ngspice_inputs/spice_models/45nm_bulk.txt
export PATH=$HOME/ngspice-27/opt/bin:/$PATH
./ngspice_checker.py --netlist final_design.cir --spec <spec>.json

// Ver2., in public class server
// Be aware of whether you are using bash or tcsh
source PA4_demo.sh
/tmp/ngspice_model/ngspice_checker.py --netlist final_design.cir --spec <spec>.json
```

### How to plot
```
python episode.py --csv <progress>.csv
```
