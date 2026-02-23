echo "Copying AutoCkt_demo..."
cp -r /tmp/AutoCkt_demo ~
echo "Initial environment..."

# In bash
# export RAY_TMPDIR=~/AutoCkt_demo/ray_tmp
# In tcsh
setenv RAY_TMPDIR ~/AutoCkt_demo/ray_tmp

echo "Finish initializing PA4 demo!"
echo "Execute sizing_[student_ID].py and ngspice_checker.py inside 1st level of AutoCkt_demo"
echo "(i.e. same level with autockt/, eval_engines/)"
