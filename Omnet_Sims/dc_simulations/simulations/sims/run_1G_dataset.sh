#!/bin/bash


do_extract () {
    python3 ./extractor_shell_creator.py $1
    pushd ./results/
    bash extractor.sh
    popd
    sleep 5
}

rm -rf results
rm -rf logs
rm -rf figs
rm -rf extracted_results
rm -rf results_1G

# create the directory to save extracted_results
bash dir_creator.sh


sudo chmod -R +777 ./


echo "\n\n-------------------------------------------"
echo "Running DCTCP_SD"
opp_runall -j50 ../../src/dc_simulations -m -u Cmdenv -c DCTCP_SD -n ..:../../src:../../../inet/src:../../../inet/examples:../../../inet/tutorials:../../../inet/showcases --image-path=../../../inet/images -l ../../../inet/src/INET omnetpp_1G.ini
do_extract dctcp_sd
mkdir logs/dctcp_sd_1G
cp results/*.out logs/dctcp_sd_1G/

# move the extracted results
echo "Moving the extracted results to results_1G"
rm -rf results_1G
mv extracted_results results_1G

echo "creating dataset"
python3 create_dataset.py
echo "dataset created"

echo "filtering collisions"
python3 filter_collisions.py
echo "collisions filtered" 

echo "filtering overlapping timestamps"
python3 filter_overlapping_timestamps.py
echo "overlapping timestamps filtered"
# python3 sample_qct.py