#!/bin/bash
error=0
max=1000
for ((i=0; i<$max; i++));
do
    yes "\n" | ./rdt_sim 1000 0.1 100 0.3 0 0.3 0 | grep "Congratulations" >> /dev/null
    if [ $? -eq 1 ]; then
        error=$(($error + 1))
    fi
done

echo $error"/"$max