import pandas as pd
import matplotlib.pyplot as plt
import argparse
import os
import numpy as np

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", type=str, required=True, help="csv file path")
    args = parser.parse_args()

    if not os.path.exists(args.csv):
        print(f"Error: cannot find file {args.csv}")
        return

    df = pd.read_csv(args.csv)
    df = df.dropna(subset=['training_iteration', 'episode_reward_mean', 'episode_len_mean'])
    
    iters = df['training_iteration'].values
    rewards = df['episode_reward_mean'].values
    lengths = df['episode_len_mean'].values

    plt.style.use('seaborn-v0_8-muted')
    fig, ax1 = plt.subplots(figsize=(13, 7))
    ax2 = ax1.twinx() 

    line1, = ax1.plot(iters, rewards, color='#0077cc', linewidth=1, zorder=3)
    line2, = ax2.plot(iters, lengths, color='#ff8800', linewidth=1, linestyle='--', zorder=3)

    ax1.set_xlim(0, iters.max())
    ax1.set_ylim(rewards.min() - 5, rewards.max() + 5)
    ax2.set_ylim(lengths.min() - 2, lengths.max() + 2)

    for ax in [ax1, ax2]:
        for spine in ax.spines.values():
            spine.set_visible(True)
            spine.set_linewidth(1)
            spine.set_edgecolor('#222222')

    ax1.set_xlabel("Training Iteration", fontsize=11, fontweight='bold')
    ax1.set_ylabel("Mean Reward", color='#0077cc', fontsize=11, fontweight='bold')
    ax2.set_ylabel("Mean Length", color='#ff8800', fontsize=11, fontweight='bold')
    ax1.grid(True, linestyle='--', alpha=0.3, zorder=0)

    v_line = ax1.axvline(color='#555555', alpha=0.7, linestyle=':', linewidth=1.2, visible=False, zorder=10)
    h_line_re = ax1.axhline(color='#0077cc', alpha=0.6, linestyle='--', linewidth=1, visible=False, zorder=10)
    h_line_le = ax2.axhline(color='#ff8800', alpha=0.6, linestyle='--', linewidth=1, visible=False, zorder=10)

    info_box_props = dict(boxstyle="round,pad=0.5,rounding_size=0.1", 
                          facecolor="#1a252f", 
                          edgecolor="none", 
                          alpha=0.95)
    
    info_text = fig.text(0.5, 0.96, "SYSTEM ACTIVE: MOVE MOUSE TO INSPECT DATA", 
                         ha="center", va="center", color="#ecf0f1", fontsize=11, 
                         fontfamily='monospace', fontweight='bold',
                         bbox=info_box_props)

    def on_move(event):
        if event.inaxes:
            idx = (np.abs(iters - event.xdata)).argmin()
            it, re, le = iters[idx], rewards[idx], lengths[idx]

            v_line.set_xdata([it, it])
            v_line.set_visible(True)

            h_line_re.set_ydata([re, re])
            h_line_re.set_visible(True)

            h_line_le.set_ydata([le, le])
            h_line_le.set_visible(True)
            
            display_str = f" [ ITER: {int(it):4d} ] [ REWARD: {re:3.3f} ] [ LENGTH: {le:3.1f} ] "
            info_text.set_text(display_str)
            
            fig.canvas.draw_idle()
        else:
            v_line.set_visible(False)
            h_line_re.set_visible(False)
            h_line_le.set_visible(False)
            fig.canvas.draw_idle()

    fig.canvas.mpl_connect('motion_notify_event', on_move)

    plt.tight_layout(rect=[0, 0.03, 1, 0.93]) 
    
    plt.show()

if __name__ == "__main__":
    main()