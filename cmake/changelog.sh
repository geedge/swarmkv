#!/bin/sh

branch=`git status | grep branch | awk '{print $NF}'`
git log --branches=$branch --no-merges --date=local --show-signature --pretty="* %ad %an %ae %nhash: %H%ncommit:%n%B"  | awk -F"-" '{print "- "$0}' | sed 's/- \*/\*/g' | sed 's/- $//g' | sed 's/-/  -/g' | sed 's/[0-9]\{2\}:[0-9]\{2\}:[0-9]\{2\}//g' > changelog.txt
