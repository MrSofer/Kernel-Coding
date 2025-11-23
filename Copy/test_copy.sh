#!/bin/bash

cleanup(){
rm -rf test_files/
}

trap cleanup EXIT

start=$(date +%s%N) #I used %N incase test takes less than a second
BUFFER_SIZE=512
PASS_COUNT=0
FAIL_COUNT=0
PASS_TESTS=6
FAIL_TESTS=3
TOTAL_TESTS=9

mkdir -p test_files/

#define correctness test
correctness_test(){

    local TEST_NUM="$1"
    local DD="$2"
    local BUFFER_SIZE="$3"

    echo "Running passing test $TEST_NUM/$PASS_TESTS..."

    local INPUT_FILE="test_files/input_${TEST_NUM}.tmp"
    local OUTPUT_FILE="test_files/output_${TEST_NUM}.tmp"

    eval "$DD of=$INPUT_FILE" > /dev/null 2>&1

    ./copy "$INPUT_FILE" "$OUTPUT_FILE" "$BUFFER_SIZE" > /dev/null 2>&1

    if [ $? -ne 0 ]; then
        echo "FAIL: $?"
        return 1
    fi

    local CHKSUM_IN=$(md5sum "$INPUT_FILE" | cut -d' ' -f1) 
    local CHKSUM_OUT=$(md5sum "$OUTPUT_FILE" | cut -d' ' -f1)

    if [ "$CHKSUM_IN" = "$CHKSUM_OUT" ]; then
        echo "PASS"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "FAIL: Checksums mismatch."
    fi
}
#run correctness tests
correctness_test 1 "dd if=/dev/zero bs=1 count=0" 64
correctness_test 2 "dd if=/dev/zero bs=10 count=1" 64
correctness_test 3 "dd if=/dev/zero bs=1024 count=1" 512
correctness_test 4 "dd if=/dev/zero bs=1M count=1" 512
correctness_test 5 "dd if=/dev/zero bs=10M count=1" 4096
correctness_test 6 "dd if=/dev/urandom bs=1M count=1" 4096


#run error tests
echo "Running error tests..."
echo "Nonexisting input..."
./copy "test_files/nonexistent_input" "test_files/out_temp" 64  > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "PASS: program failed successfully"
    FAIL_COUNT=$((FAIL_COUNT + 1))
else
    echo "FAIL: copy worked"
fi

echo "Existing output..."
touch "test_files/existing_output"
./copy "test_files/input_4.tmp" "test_files/existing_output" 512  > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "PASS: program failed successfully"
    FAIL_COUNT=$((FAIL_COUNT + 1))
else
    echo "FAIL: copy worked"
fi
rm -f "test_files/existing_output"

echo "Not enough arguments..."
./copy "test_files/input_Medium_file.tmp" "test_files/out_temp" > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "PASS: program failed successfully"
    FAIL_COUNT=$((FAIL_COUNT + 1))
else
    echo "FAIL: copy worked"
fi

echo "Summary:"
echo ""
echo "Correctness tests completed: $PASS_COUNT/$PASS_TESTS"
echo "Error tests completed: $FAIL_COUNT/$FAIL_TESTS"
echo "Total tests completed: $((PASS_COUNT + FAIL_COUNT))/$TOTAL_TESTS"

end=$(date +%s%N)
elapsed=$(((end - start)/ 1000000))

echo "Elapsed (seconds): $((elapsed / 1000)).$((elapsed % 1000))"

cleanup
