# Lab work 9: proftools
Authors (team): [Kyrylo Omelianchuk](github.com/kyrylo-om), [Daryna Shevchuk](github.com/dasha-pn)<br>
## Prerequisites

cmake\
GCC\
python 3.13.3

## Compilation

```
dos2unix compile.sh
./compile.sh
```
for info:
```
./compile.sh -h
```

## Installation

TBB library

```
sudo apt update
sudo apt install libtbb-dev
```

For plots and csv`s

```
pip install -r requirements.txt
```

### Usage

To run the tests:

```
python3 scripts/test_words.py tools
```

To run the autolaunch:

```
python3 scripts/autolaunch.py   --exe ./bin/countwords_par_proftools   --configs config/config1.txt config/config2.txt   --threads 1 2 4 8 16   --runs 5
```

### Results

In folder `report`
