import os
import sys
import subprocess
import getopt

SCHEME_NAME = "LoadAddressDemo"
WORKSPACE_NAME = "LoadAddressDemo"
BUILD_DERIVED_DIR = "release"
WAIT_PARSE_SYMBOLS = []
OFFSETS = []

def build_ipa():

    # subprocess.run("rm -rf {release_dir}".format(release_dir=BUILD_DERIVED_DIR), shell=True)

    print("============================ building ===============================")

    build_cmd = "xcrun xcodebuild -workspace {workspace}.xcworkspace -scheme {scheme} -configuration Release -destination generic/platform=iOS -derivedDataPath {derived_path}".format(workspace = WORKSPACE_NAME, scheme = SCHEME_NAME, derived_path = BUILD_DERIVED_DIR)
    process = subprocess.Popen(build_cmd, shell=True)
    process.wait()
    if process.returncode != 0:
        print("!!!!!!!!!!! build failure !!!!!!!!!!")
        sys.exit()
    print("============================ ipa gernerated ===============================")

    parse_symbol_table()

def parse_symbol_table():

    ipa_dir = "{derived_path}/Build/Products/Release-iphoneos/{ipa_name}.app".format(derived_path = BUILD_DERIVED_DIR, ipa_name = SCHEME_NAME)
    os.chdir(ipa_dir)
    print(subprocess.check_output("pwd"))

    extract_symbol_cmd = "nm -arch arm64 {executable_name} | grep {filter_content}".format(executable_name = SCHEME_NAME, filter_content = WAIT_PARSE_SYMBOLS[0])
    print(extract_symbol_cmd)
    symbol_table_row = subprocess.check_output(extract_symbol_cmd, shell=True)
    print(symbol_table_row)
    row_elements = symbol_table_row.decode('utf-8').split(' ')
    
    if len(row_elements) < 1:
        print("!!!!!!!!!!! not find specific symbol !!!!!!!!!!")
        sys.exit()

    relative_symbol_address = row_elements[0]
    print(relative_symbol_address)

    offset_instruction_address = hex(int(relative_symbol_address, base=16) + OFFSETS[0])
    print(offset_instruction_address)

    symbolicate_cmd = "atos -o LoadAddressDemo -arch arm64 {symbol_address} {offset_instruction_address}".format(symbol_address=relative_symbol_address, offset_instruction_address = offset_instruction_address)
    print(symbolicate_cmd)
    print('\n\n ------ Symbolicate Result ------\n')
    subprocess.call(symbolicate_cmd, shell=True)

def main(argv):
    help_desc = 'symbolicator.py -s <symbol> -o <offset>'

    try:
        opts, args = getopt.getopt(argv,"hs:o:",["symbol=","offset="])
    except getopt.GetoptError:
        print(help_desc)
        sys.exit(2)

    for opt, arg in opts:
        if opt == '-h':
            print(help_desc)
            sys.exit()
        elif opt in ("-s", "--symbol"):
            WAIT_PARSE_SYMBOLS.append(arg)
        elif opt in ("-o", "--offset"):
            OFFSETS.append(int(arg))

    build_ipa()

if __name__ == "__main__":
    main(sys.argv[1:])
    