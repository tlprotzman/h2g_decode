#bash

function SingleFileChangeVersion()
{
  #fixed version change
  vOrg="# File Version: 0.13"
  vMod="# File Version: 0.14"

  fileNameBase=$1
  nLineRM=$2
  
  echo $fileNameBase
  echo $nLineRM
  rm $fileNameBase\_mod.h2g
  
  cp $fileNameBase.h2g $fileNameBase\_test.h2g 

  # echo "first $nLineRM line unmod file"
  # cat $fileNameBase\_test.h2g | head -n $nLineRM

  numL=`wc -l $fileNameBase\_test.h2g | cut -d " " -f 1`
  echo "file has $numL lines"
  
  cat $fileNameBase\_test.h2g | tail -n $(($numL +1 - $nLineRM)) > $fileNameBase\_headerRM.h2g 
  cat $fileNameBase\_test.h2g | head -n $nLineRM > $fileNameBase\_header.h2g 

  echo "header file" 
  cat $fileNameBase\_header.h2g
  # echo "first $nLineRM line mod file"
  # cat $fileNameBase\_headerRM.h2g | head -n $nLineRM

  export DATA="# Number of KCUs: $3\\
# Number of ASICs: $4\\
# Data Acquisition Mode: manual"

  ToReplace="# Data Acquisition Mode: manual"

  sed "s/$ToReplace/${DATA}/g" $fileNameBase\_header.h2g > $fileNameBase\_headerMod.h2g
  # echo "header file modified" 
  # cat $fileNameBase\_headerMod.h2g

  sed "s/$vOrg/$vMod/g" $fileNameBase\_headerMod.h2g > $fileNameBase\_headerMod2.h2g
  # echo "header file modified also version" 
  # cat $fileNameBase\_headerMod2.h2g

  cat $fileNameBase\_headerMod2.h2g $fileNameBase\_headerRM.h2g > $fileNameBase\_mod.h2g
  numN=`wc -l $fileNameBase\_mod.h2g | cut -d " " -f 1`
  echo "new file has $numL lines"
  echo "first $(($nLineRM+3)) line mod file"
  cat $fileNameBase\_mod.h2g | head -n $(($nLineRM+4))

  rm $fileNameBase\_headerMod2.h2g $fileNameBase\_headerMod.h2g $fileNameBase\_headerRM.h2g $fileNameBase\_header.h2g $fileNameBase\_test.h2g 

}

function ChangeNumberOfAsics()
{

  #fixed version change
  vOrg="# File Version: 0.13"
  vMod="# File Version: 0.14"

  fileNameBase=$1
  nLineRM=$2
  
  echo $fileNameBase
  echo $nLineRM
  rm $fileNameBase\_mod.h2g
  
  cp $fileNameBase.h2g $fileNameBase\_test.h2g 

  # echo "first $nLineRM line unmod file"
  # cat $fileNameBase\_test.h2g | head -n $nLineRM

  numL=`wc -l $fileNameBase\_test.h2g | cut -d " " -f 1`
  echo "file has $numL lines"
  
  cat $fileNameBase\_test.h2g | tail -n $(($numL +1 - $nLineRM)) > $fileNameBase\_headerRM.h2g 
  cat $fileNameBase\_test.h2g | head -n $nLineRM > $fileNameBase\_header.h2g 

  echo "header file" 
  cat $fileNameBase\_header.h2g
  echo "first 1 line mod file"
  cat $fileNameBase\_headerRM.h2g | head -n 1

  export DATA="# Number of ASICs: $4"
  ToReplace="# Number of ASICs: $3"

  echo "\n\n\n"
  echo $ToReplace
  echo ${DATA}
  
  echo "\n\n\n"
  sed "s/$ToReplace/${DATA}/g" $fileNameBase\_header.h2g > $fileNameBase\_headerMod.h2g
  # echo "header file modified" 
  cat $fileNameBase\_headerMod.h2g


  export DATA="# Generator Setting data_coll_enable: $6"
  ToReplace="# Generator Setting data_coll_enable: $5"
    echo "\n\n\n"
  echo $ToReplace
  echo ${DATA}
  echo "\n\n\n"
  sed "s/$ToReplace/${DATA}/g" $fileNameBase\_headerMod.h2g > $fileNameBase\_headerMod2.h2g
  # echo "header file modified" 
  cat $fileNameBase\_headerMod2.h2g

  echo "\n\n\n"
  cat $fileNameBase\_headerMod2.h2g $fileNameBase\_headerRM.h2g > $fileNameBase\_mod.h2g
  numN=`wc -l $fileNameBase\_mod.h2g | cut -d " " -f 1`
  echo "new file has $numL lines"
  echo "first $(($nLineRM)) line mod file"
  cat $fileNameBase\_mod.h2g | head -n $(($nLineRM+1))

  rm $fileNameBase\_headerMod.h2g $fileNameBase\_headerRM.h2g $fileNameBase\_header.h2g $fileNameBase\_test.h2g 

}

# base=/media/fbock/LFHCal2/202511_PST09/raw/TBMain2025/Run

# KCUs=2
# ASICs=4
# runs='006 010 011 012 013 016 017 018 019 020 021 022 023 024 025 026 027 028 029 030 031 032 033 034 035 036 038 039 040 041 042 043 046 047 048 049 050 051 052 053 054 055 056  057 058 059 068 069 070 071 072 073 074 075 076 122' 
# runs='007 008 009 037' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;
# 
# KCUs=2
# ASICs=3
# runs='123' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;
# 
# KCUs=2
# ASICs=2
# runs='124' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;
# 
# KCUs=2
# ASICs=1
# runs='125' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;
# 
# KCUs=1
# ASICs=4
# runs='126' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;
# 
# KCUs=1
# ASICs=3
# runs='127' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;
# 
# KCUs=1
# ASICs=2
# runs='128' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;
# 
# KCUs=1
# ASICs=1
# runs='129' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;
# 
# KCUs=2
# ASICs=4
# runs='130 131 132 133 134 135 136 137 138 139 140 141 145 146 147 148 149 150 ' 
# for runNr in $runs; do 
#   echo $runNr
#   SingleFileChangeVersion $base$runNr 23 $KCUs $ASICs
# done;


# run 154 first with fully new data format (incl v.014 & KCUs & ASIC numbers)


# base1=/media/fbock/LFHCal2/202511_PST09/raw/TBMain2025/Run
# base2=/media/fbock/LFHCal2/202511_PST09/raw/TBMain2025Fixed/Run
# for i in $(seq -f "%03g" 10 200)
# do
#   echo $i
#   if [ -f $base1$i\_mod.h2g ]; then
#     echo moving $i
#     mv $base1$i\_mod.h2g $base2$i.h2g
#   fi
# done

base1=/media/fbock/ALICE2-4TB/202607_FoCalTB/rawTesting/Run
base2=/media/fbock/ALICE2-4TB/202607_FoCalTB/rawTesting/fixed/Run
for i in $(seq -f "%03g" 205 222)
# for i in $(seq -f "%03g" 230 230)
do
  echo $i
  ChangeNumberOfAsics $base1$i 25 2 3 255 5
  if [ -f $base1$i\_mod.h2g ]; then
    echo moving $i
    mv $base1$i\_mod.h2g $base2$i.h2g
  fi
done

