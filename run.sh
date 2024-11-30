./waf configure
cp $2/$3  $2/trace_C0.trc.shared
./waf --run "scratch/MultiCoreSimulator --CfgFile=$1 --BMsPath=$2 --LogFileGenEnable=0"
