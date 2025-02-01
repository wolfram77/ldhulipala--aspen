#!/usr/bin/env bash
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=64
#SBATCH --exclusive
#SBATCH --job-name slurm
#SBATCH --output=slurm.out
# source scl_source enable gcc-toolset-11
# module load hpcx-2.7.0/hpcx-ompi
# module load openmpi/4.1.5
# module load cuda/12.3
source /opt/rh/gcc-toolset-13/enable
src="ldhulipala--aspen"
out="$HOME/Logs/$src$1.log"
ulimit -s unlimited
printf "" > "$out"

# Download program
if [[ "$DOWNLOAD" != "0" ]]; then
  rm -rf $src
  git clone https://github.com/wolfram77/$src
  cd $src
fi

# Install gve.sh
if [[ "$INSTALL" != "0" ]]; then
  echo "Installing gve.sh ..."
  npm install -g gve.sh
fi

# Compile
cd code
make clean
make -j32
cd ..

# Run on a single graph
runOne() {
  # $1: input file name
  # $2: is graph weighted (0/1)
  # $3: is graph symmetric (0/1)
  # Convert the graph in MTX format to Adjacency graph format
  optw=""
  opts=""
  if [[ "$2" == "1" ]]; then optw="-w"; fi
  if [[ "$3" == "1" ]]; then opts="-s"; fi
  printf "Converting $1 to $1.adj ...\n"  | tee -a "$out"
  gve no-operation -i "$1" -o "$1.adj" -g adj $optw $opts 2>&1 | tee -a "$out"
  # Run the program
  stdbuf --output=L ./code/run_batch_updates "$1.adj" $2 $3 2>&1 | tee -a "$out"
  # Cleanup
  rm -f "$1.adj"
}

# Run on all graphs
runAll() {
  # runOne "$HOME/Data/web-Stanford.mtx"    0 0
  runOne "$HOME/Data/indochina-2004.mtx"  0 0
  runOne "$HOME/Data/uk-2002.mtx"         0 0
  runOne "$HOME/Data/arabic-2005.mtx"     0 0
  runOne "$HOME/Data/uk-2005.mtx"         0 0
  runOne "$HOME/Data/webbase-2001.mtx"    0 0
  runOne "$HOME/Data/it-2004.mtx"         0 0
  runOne "$HOME/Data/sk-2005.mtx"         0 0
  runOne "$HOME/Data/com-LiveJournal.mtx" 0 1
  runOne "$HOME/Data/com-Orkut.mtx"       0 1
  runOne "$HOME/Data/asia_osm.mtx"        0 1
  runOne "$HOME/Data/europe_osm.mtx"      0 1
  runOne "$HOME/Data/kmer_A2a.mtx"        0 1
  runOne "$HOME/Data/kmer_V1r.mtx"        0 1
}

# Run 5 times
for i in {1..5}; do
  runAll
done

# Signal completion
curl -X POST "https://maker.ifttt.com/trigger/puzzlef/with/key/${IFTTT_KEY}?value1=$src$1"
