output="dij_benchmark_$(date "+%Y-%m-%d-%H-%M-%S").txt"
touch "$output"
# gcc ./dij-bench.cpp -o dij-bench.exe -lstdc++ -lbenchmark -pthread
# echo "dij-bench without O2 compiled"
# ./dij-bench.exe &>> $output
# echo "dij-bench without O2 runned"
# gcc ./dij-bench.cpp -o dij-bench.exe -O2 -lstdc++ -lbenchmark -pthread
# echo "dij-bench with O2 compiled"
# ./dij-bench.exe &>> $output
# echo "dij-bench with O2 runned"
gcc ./dij-bench-hack-spfa.cpp -o dij-bench.exe -lstdc++ -lbenchmark -pthread
echo "dij-bench hacking SPFA without O2 compiled"
./dij-bench.exe &>> $output
echo "dij-bench hacking SPFA without O2 runned"
gcc ./dij-bench-hack-spfa.cpp -o dij-bench.exe -O2 -lstdc++ -lbenchmark -pthread
echo "dij-bench hacking SPFA with O2 compiled"
./dij-bench.exe &>> $output
echo "dij-bench hacking SPFA with O2 runned"
echo "All benchmark runned"
