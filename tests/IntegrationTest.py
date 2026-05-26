import os, re, subprocess
import pathlib
import argparse
from typing import Union

TEST_PASSED = 0
TOTAL_TEST_COUNT = 0
TESTED_UNIT_COUNT = 0

class Colors:
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'

parser = argparse.ArgumentParser()
parser.add_argument('-c', '--clear', action='store_true')
args = parser.parse_args()

def read_command(line:str, file_name:str) -> str:
    return re.sub("%\S+%", file_name, line)

def parse_command(line:str) -> list:
    return re.sub("//\s*RUN:\s+", "", line).split()

def parse_loop_args(line:str) -> list:
    return re.sub("//\s*FOR:\s+", "", line).split()

def check_command(line: str) -> bool:
    """
    Checking is line test directive or not
    """
    line = line.strip()
    if line.startswith("//RUN:") or \
       line.startswith("//FOR:") or \
       line.startswith("//ENDFOR"):
           return True
    return False

def execute_command(command: list) -> int:
    """
    Execute command via python subprocess and return status code:
    On success: 1
    On fail: 0
    """
    print(f"Execute command: [{' '.join(command)}]")
    proc = subprocess.run(
        command,
        cwd=os.getcwd(),
        capture_output=True
    )
    output = proc.stdout.decode()
    if len(output) != 0:
        print(f"Output: {output}")
    if proc.returncode != 0:
        print(f"Status: {Colors.FAIL}Fail{Colors.ENDC}")
        return 0
    else:
        print(f"Status: {Colors.GREEN}OK{Colors.ENDC}")
        return 1

def test_specific_file(lines: list) -> Union[int, int]:
    """
    Handle test directives which was found in file.
    
    Return tuple of (test_passed, total_test_count)
    """
    test_passed = 0
    total_test_count = 0
    i = 0
    stack = []
    memory = {}
    while i < len(lines):
        if(lines[i].startswith("//RUN:")):
            if len(stack) != 0:
                raw_command = read_command(lines[i], f"tests/{obj}/{test_case}")
                for (argName, args) in memory.items():
                    raw_command = raw_command.replace(argName, args[0])
                command = parse_command(raw_command)
            else:
                command = parse_command(read_command(lines[i], f"tests/{obj}/{test_case}"))
            test_passed += execute_command(command)
            total_test_count += 1
            print('----')
        elif lines[i].startswith("//FOR:"):
            argName, *args = parse_loop_args(read_command(lines[i], f"tests/{obj}/{test_case}"))
            stack.append((i, argName))
            memory[argName] = args
        elif lines[i].startswith("//ENDFOR"):
            args = memory[stack[-1][1]]
            args.pop(0)
            if len(args) == 0:
                del memory[stack[-1][1]] # remove local var from memory
                stack.pop(-1) # remove loop frame from stack
            else:
                i = stack[-1][0]
        i += 1
    return (test_passed, total_test_count)


for obj in os.listdir("tests"):
    if os.path.isdir(f"tests/{obj}"):
        print(f"{Colors.WARNING}Starting tests from {obj}:{Colors.ENDC}")
        for test_case in os.listdir(f"tests/{obj}"):
            if(test_case.endswith(obj)):
                print(f"{Colors.CYAN}Testing file: {obj}/{test_case}{Colors.ENDC}")
                with open(f"tests/{obj}/{test_case}") as file:
                    lines = [line.strip() for line in file.readlines() if check_command(line)]
                    test_passed, total_test_count = test_specific_file(lines)
                    TEST_PASSED += test_passed
                    TOTAL_TEST_COUNT += total_test_count
                    TESTED_UNIT_COUNT += 1
                print()
        print()

# --- Work with command line arguments ---
# remove from repo binary files after test
if args.clear:
    for obj in os.listdir("tests"):
        if os.path.isdir(f"tests/{obj}"):
            for file in os.listdir(f"tests/{obj}"):
                if file.endswith(".o") or file.endswith(".res"):
                    pathlib.Path.unlink(
                        pathlib.Path(f"tests/{obj}/{file}"),
                        missing_ok=True
                    )

if TEST_PASSED == TOTAL_TEST_COUNT:
    print(f"Number of tested files: {TESTED_UNIT_COUNT}.")
    print(f"Test passing: {Colors.GREEN}{TEST_PASSED}/{TOTAL_TEST_COUNT}{Colors.ENDC}")
    exit(0)
if TEST_PASSED != TOTAL_TEST_COUNT:
    print(f"Number of tested files: {TESTED_UNIT_COUNT}.")
    print(f"Test passing: {Colors.FAIL}{TEST_PASSED}/{TOTAL_TEST_COUNT}{Colors.ENDC}")
    exit(1) # call exit 1 to trigger error in CI system
