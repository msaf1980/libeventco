#!/usr/bin/env bash

TESTS="../bench/bench_yield ./test_cond"

cd `dirname $0`

res=0
pass=0
fails=0
failed=""
echo "TESTS"

for test in ${TESTS} ; do
	echo "RUN ${test}"
	${test}
	if [ "$?" == "0" ]; then
		echo "PASS"
		: $((pass++))
	else
		echo "FAIL"
		: $((fail++))
		failed="${failed} ${test}"
		res=1
	fi
done

echo "pass ${pass}"
if [ "${res}" == "1" ]; then
	echo "failed ${fails}:${failed}"
fi
exit ${res}
