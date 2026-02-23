import argparse
import json
import os
import ray
import numpy as np
import pickle
from ray.rllib.agents.registry import get_agent_class
from ray.tune.registry import register_env
from autockt.envs.ngspice_vanilla_opamp import TwoStageAmp

DEBUG = True
K = 10
MAX_TRIALS = 200

# ----------------------------------------------------------------------------------
# SAFETY MARGINS: [Gain (dB), Ibias (A), PhM (deg), UGBW (Hz)]
# Adjusted_Target = Original_Target + Margin
# 
# Constraint Tightening (Normalization Formulas):
# - MIN Specs (Gain, PhM, UGBW): (Measured - Target) / (Measured + Target) > 0.02
# - MAX Specs (Ibias):           (Target - Measured) / (Target + Measured) > 0.02
#
# Reference: See reward function in AutoCkt/autockt/envs/ngspice_vanilla_opamp.py
# ----------------------------------------------------------------------------------
MARGIN = [
    [13, -3e-04, 3, 9e5], # spec1.json
    [15, -4e-05, 3, 9e5]  # spec2.json
]

def run_sizing():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=str, required=True, help="Path to checkpoint file")
    parser.add_argument("--spec", type=str, required=True, help="Path to spec JSON")
    args = parser.parse_args()

    with open(args.spec, 'r') as f:
        target_spec = json.load(f)
    
    if "spec1" in args.spec:
        m_val = MARGIN[0]
        # print("Detected spec1: Applying MARGIN[0]")
    elif "spec2" in args.spec:
        m_val = MARGIN[1]
        # print("Detected spec2: Applying MARGIN[1]")
    else:
        m_val = [0, 0, 0, 0]
        print("Warning: Spec name unrecognized. No margin applied.")

    m_gain = target_spec['gain_min'] + m_val[0]
    m_ibias = target_spec['ibias_max'] + m_val[1]
    m_phm = target_spec['phm_min'] + m_val[2]
    m_ugbw = target_spec['ugbw_min'] + m_val[3] 

    new_ideal = np.array([m_gain, m_ibias, m_phm, m_ugbw])

    print("Start sizing...")
    ideal_list = [target_spec['gain_min'], target_spec['ibias_max'], target_spec['phm_min'], target_spec['ugbw_min']]
    print("Ideal spec:  [%s]" % ", ".join(["%.2e" % s for s in ideal_list]))
    print("Margin spec: [%s]" % ", ".join(["%.2e" % s for s in new_ideal.tolist()]))

    # Initialize Ray
    ray.shutdown()
    tmp_dir = os.environ.get("RAY_TMPDIR", os.path.expanduser("~/AutoCkt_demo/ray_tmp"))
    ray.init(temp_dir=tmp_dir, ignore_reinit_error=True)
    register_env("opamp-v0", lambda cfg: TwoStageAmp(env_config=cfg))

    # Manual config agent
    manual_config = {
        "env": "opamp-v0",
        "num_workers": 0,
        "num_gpus": 0,
        "horizon": 60,
        "model": {"fcnet_hiddens": [64, 64]},
        "env_config": {"generalize": True, "run_valid": False, "save_specs": False}
    }

    # Agent instantiation and Weight injection
    cls = get_agent_class("PPO") 
    agent = cls(config=manual_config)

    print("Loading raw weights from: %s" % args.model)
    with open(args.model, "rb") as f:
        checkpoint_data = pickle.load(f)
    
    try:
        agent.__setstate__(checkpoint_data)
        print("Agent state restored successfully.")
    except Exception as e:
        if "worker" in checkpoint_data:
            agent.local_evaluator.restore(checkpoint_data["worker"])
            print("Restored via local_evaluator.restore")
        else:
            raise e

    # 5. Env initialization
    env = TwoStageAmp(env_config=manual_config["env_config"])

    # 6. Inference
    done = False
    iteration = 1
    
    while not done:
        print("Start Iteration %d" % iteration)
        state = env.reset()
        env.specs_ideal = new_ideal
        env.specs_ideal_norm = env.lookup(env.specs_ideal, env.global_g)
        cur_spec_norm = env.lookup(env.cur_specs, env.global_g)
        state = np.concatenate([cur_spec_norm, env.specs_ideal_norm, env.cur_params_idx])
        
        steps = 0
        while steps < MAX_TRIALS:
            steps += 1
            
            should_print = DEBUG and (steps % K == 0)
            if should_print:
                print("Trial %d" % steps)

            action = agent.compute_action(state)
            state, reward, done, info = env.step(action)

            if done:
                print("Success at Iter %d Trial %d" % (iteration, steps))
                break

            if should_print:
                print("Current Idx:    %s" % env.cur_params_idx.tolist())
                measured_sci = ["%.6e" % s for s in env.cur_specs.tolist()]
                print("Measured specs: [%s]" % ", ".join(measured_sci))
                print("Reward: %f\n" % reward)
        
        if done:
            break
        else:
            print("Reached %d trials without success. Restarting..." % MAX_TRIALS)
            iteration += 1

    p_idx = env.cur_params_idx
    p_dict = {env.params_id[i]: env.params[i][p_idx[i]] for i in range(len(env.params_id))}
    
    print("-" * 10)
    print("Final Parameters")
    for k in ['mp1', 'mn1', 'mn3', 'mp3', 'mn4', 'mn5']:
        print("%s: %s" % (k, p_dict[k]))
    print("cc: %s" % p_dict['cc'])
    print("-" * 10)

    template = """two_stage_amp test
* Two stage OPAMP
.include /tmp/ngspice_model/45nm_bulk.txt
*********** TODO ***********
.param wp1=0.5u lp1=90n mp1=%d
.param wn1=0.5u ln1=90n mn1=%d
.param wn3=0.5u ln3=90n mn3=%d
.param wp3=0.5u lp3=90n mp3=%d
.param wn4=0.5u ln4=90n mn4=%d
.param wn5=0.5u ln5=90n mn5=%d
.param cc=%s
****************************
.param ibias=30u
.param cload=10p
.param vcm=0.6
mp1 net4 net4 VDD VDD pmos w=wp1 l=lp1 m=mp1
mp2 net5 net4 VDD VDD pmos w=wp1 l=lp1 m=mp1
mn1 net4 net2 net3 net3 nmos w=wn1 l=ln1 m=mn1
mn2 net5 net1 net3 net3 nmos w=wn1 l=ln1 m=mn1
mn3 net3 net7 VSS VSS nmos w=wn3 l=ln3 m=mn3
mn4 net7 net7 VSS VSS nmos w=wn4 l=ln4 m=mn4
mp3 net6 net5 VDD VDD pmos w=wp3 l=lp3 m=mp3
mn5 net6 net7 VSS VSS nmos w=wn5 l=ln5 m=mn5
cc net5 net6 cc
ibias VDD net7 ibias
vin in 0 dc=0 ac=1.0
ein1 net1 cm in 0 0.5
ein2 net2 cm in 0 -0.5
vcm cm 0 dc=vcm
vdd VDD 0 dc=1.2
vss 0 VSS dc=0
CL net6 0 cload
.end
""" % (
        int(p_dict['mp1']), int(p_dict['mn1']),
        int(p_dict['mn3']), int(p_dict['mp3']),
        int(p_dict['mn4']), int(p_dict['mn5']),
        str(p_dict['cc'])
    )

    with open("final_design.cir", "w") as f:
        f.write(template)
    
    print("End sizing.")
    ray.shutdown()

if __name__ == "__main__":
    run_sizing()