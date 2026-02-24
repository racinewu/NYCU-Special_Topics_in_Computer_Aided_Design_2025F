# Installation Guide

> [!TIP]
> Please refer to the TA's tutorial for other instructions.

1. **Setup Linux Environment**: Follow the manual or use native linux environment.
2. **Install Anaconda**
    1. Return Home and download Anaconda
        ```
        cd ~
        wget https://repo.anaconda.com/archive/Anaconda3-2025.06-0-Linux-x86_64.sh
        ```
    2. Run the installation script
        ```
        bash Anaconda3-2025.06-0-Linux-x86_64.sh
        [Enter] -> 'yes' -> [Enter] -> 'no'
        ```
    3. Enviroment setup
        ```
        ~/anaconda3/bin/conda --version    // Check if installation successfully

        ~/anaconda3/bin/conda init bash    // Initialization
        source ~/.bashrc

        conda --version                    // Check the version
        ```
3. **Install NGSpice**: 
    1. Return Home, download the `ngspice-27.tar.gz` archive and extract it
        ```
        cd ~
        wget https://sourceforge.net/projects/ngspice/files/ng-spice-rework/old-releases/27/ngspice-27.tar.gz
        tar -zxvf ngspice-27.tar.gz
        ```
    2. Enter the ngspice directory and set up the configuration.
        ```
        cd ngspice-27
        ./configure --enable-xspice --disable-debug --with-readline=no --with-x=no --prefix=$HOME/ngspice-27/opt
        // --with-x=no: Disable ngspice's built-in graphical environment (useful if root system lacks it).
        // --prefix=$HOME/ngspice-27/opt: Set the installation directory to a local folder.
        ```
    3. Install ngspice using Makefile
        ```
        make -j`nproc`
        make install
        // -j option enables parallel compilation using multiple cores.
        // nproc returns the number of CPU cores available on the system.
        ```
    4. Check if installation successful
        ```
        ls $HOME/ngspice-27/opt/bin                   // List ngspice binaries
        export PATH=$HOME/ngspice-27/opt/bin:$PATH    // Set the environment variable
        ngspice -v                                    // Check the version
        ```
4. **Install AutoCkt**
    1. Return Home and download AutoCkt
        ```
        cd ~
        git clone https://github.com/ksettaluri6/AutoCkt.git
        ```
    2. Enter the AutoCkt directory and install the required packages using conda
        ```
        cd AutoCkt
        conda env create -f environment.yml
        conda activate autockt
        ```
    3. Modify parts of code
        - `/eval_engines/ngspice/ngspice_wrapper.py`@Line 18
            ```diff
            - BASE_TMP_DIR = os.path.abspath("/tmp/ckt_da")

            + BASE_TMP_DIR = os.path.expanduser("~/AutoCkt/ckt_da")
            ```
        - `/autockt/val_autobag_ray.py`@Line 10
            ```diff
            - ray.init()

            + import os
            + tmp = os.environ.get('RAY_TMPDIR')
            + # ray.init()
            + ray.init(temp_dir=tmp)
            ```
        - `/autockt/rollout.py` Replace with given `rollout.py`
    4. Train AutoCkt
        1. Connect library files
            ```
            python eval_engines/ngspice/ngspice_inputs/correct_inputs.py
            ```
        2. Generate spec
            ```
            python autockt/gen_specs.py --num_specs ###
            // spec files are pickles generated in autockt/gen_specs/
            // ### controls the number of specs generated. Set 350 for github reproduciton.
            ```
        3. Set the environment variable
            ```
            export PATH=$HOME/ngspice-27/opt/bin:/$PATH
            export PYTHONPATH=$PWD
            mkdir -p ray_tmp
            export RAY_TMPDIR=$HOME/AutoCkt/ray_tmp
            ```
            > **IMPORTANT**  
            > Environment Initialization: When using a different terminal session, you must execute the preceding commands. The first line is especially critical for setting up the environment.
        4. Start training
            ```
            ipython                            // Open ipython interface
            %run autockt/val_autobag_ray.py    // Start training
            // Circuits generated during training iterations are saved in: ~/AutoCkt/ckt_da/designs_two_stage_opamp/
            // Checkpoints during training are saved in: ~/ray_results/train_45nm_ngspice/PPO_opamp-v0_xxx/
            ```
    5. Validate AutoCkt
        1. Copy `episode.py` to `~/` and choose the checkpoint to use by referring to the progress.csv file.
            ```
            python episode.py --csv <csv path>
            // Status during training are saved in: ~/ray_results/train_45nm_ngspice/PPO_opamp-v0_xxx/progress.csv
            ```
        2. 
            ```
            python autockt/gen_specs.py --num_specs ###
            ```
        3. 
            ```
            %run autockt/rollout.py </abs/path/to/checkpoint> --run PPO --env opamp-v0 --num_val_specs ### --traj_len ## --no-render
            // The checkpoints are located at: ~/ray_results/train_45nm_ngspice/PPO_opampv0_xxx/checkpoint_%%%/checkpoint-%%%, xxx is the run timestamp, and %%% is the checkpoint index.
            // --num_val_specs specifies how many new validation specs are randomly sampled. Set 100 for requirement.
            // --traj_len specifies the maximum steps (trajectory length) an agent can try per spec.
            ```