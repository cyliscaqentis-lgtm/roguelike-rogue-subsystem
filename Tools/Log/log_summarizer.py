import argparse
import os
import glob
from datetime import datetime

# This script is designed to parse and summarize Unreal Engine log files.
# It filters for specific keywords like 'Error:' and 'Warning:' to reduce
# the log size, making it more manageable for AI context windows while
# retaining the most critical information.

def find_latest_file(log_directory, file_extension='log'):
    """
    Finds the most recently modified file with the specified extension in the specified directory.
    """
    if not os.path.isdir(log_directory):
        print(f"Error: Log directory not found at '{log_directory}'")
        return None

    search_pattern = os.path.join(log_directory, f'*.{file_extension}')
    files = glob.glob(search_pattern)
    if not files:
        print(f"Error: No *.{file_extension} files found in '{log_directory}' with pattern '{search_pattern}'")
        return None

    latest_file = None
    latest_mtime = 0

    for f in files:
        try:
            mtime = os.path.getmtime(f)
            if mtime > latest_mtime:
                latest_mtime = mtime
                latest_file = f
        except OSError as e:
            print(f"Warning: Could not get modification time for '{f}': {e}")
            continue

    if latest_file:
        print(f"Found latest *.{file_extension} file: '{latest_file}' (Last modified: {datetime.fromtimestamp(latest_mtime)})")
    else:
        print(f"No latest *.{file_extension} file could be determined in '{log_directory}'")

    return latest_file


def summarize_log(input_files, output_file, custom_keywords=None, depth=None):
    """
    Reads log files, filters them for relevant lines based on keywords,
    and writes the result to an output file.

    Args:
        input_files (list): A list of paths to the input log/csv files.
        output_file (str): Path to the output summary file.
        custom_keywords (list, optional): A list of additional keywords to filter for.
                                           Defaults to None.
        depth (str, optional): Predefined filtering depth ('critical', 'high', 'medium').
                               Defaults to None.
    """
    filter_keywords = []

    if depth == 'critical':
        filter_keywords.extend(['error:', 'fatal:', 'severe:'])
    elif depth == 'high':
        filter_keywords.extend(['error:', 'fatal:', 'severe:', 'warning:'])
    elif depth == 'medium' or depth is None: # 'medium' is the default depth if not specified, includes custom.
        filter_keywords.extend(['error:', 'fatal:', 'severe:', 'warning:'])
        if custom_keywords:
            filter_keywords.extend([k.lower() for k in custom_keywords])
    # Ensure no duplicates and convert to lowercase for case-insensitive matching.
    filter_keywords = list(set([k.lower() for k in filter_keywords]))

    
    print(f"Starting log summarization...")
    print(f"Input files ({len(input_files)}): {input_files}")
    print(f"Output file: {output_file}")
    print(f"Filtering for keywords (depth={depth}): {filter_keywords}")

    try:
        output_dir = os.path.dirname(output_file)
        if output_dir and not os.path.exists(output_dir): # Only create if dir specified AND does not exist
            print(f"Creating output directory: {output_dir}")
            os.makedirs(output_dir, exist_ok=True)
        else:
            print(f"Output directory not specified or already exists: {output_dir if output_dir else '.'}")

        lines_written = 0
        
        with open(output_file, 'w', encoding='utf-8') as outfile:
            print(f"Successfully opened output file: {output_file}")
            for input_file in input_files:
                print(f"Attempting to open input file: {input_file}")
                if not os.path.exists(input_file):
                    print(f"Warning: Input file not found, skipping: '{input_file}'")
                    continue
                with open(input_file, 'r', encoding='utf-8', errors='ignore') as infile:
                    print(f"Successfully opened input file: {input_file}")
                    for line in infile:
                        # Perform a case-insensitive check for any of the keywords
                        if any(keyword in line.lower() for keyword in filter_keywords):
                            outfile.write(line)
                            lines_written += 1
            print(f"Finished writing lines to output file.")

        print(f"Summarization complete. Found and wrote {lines_written} relevant lines.")

    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Summarize Unreal Engine log files by filtering for specific keywords and predefined depths.",
        formatter_class=argparse.RawTextHelpFormatter
    )

    parser.add_argument(
        "output",
        help="Path to the output summary file."
    )
    
    input_group = parser.add_mutually_exclusive_group() # Removed required=True to allow default latest in current dir
    input_group.add_argument(
        "input",
        nargs='?', # Make optional so it doesn't conflict with --latest-from-dir or --turn-range
        help="Path to a specific input log file. If not specified, one of --latest-from-dir or --turn-range must be used."
    )
    input_group.add_argument(
        "--latest-from-dir",
        nargs='?', # Make optional to allow it to be default
        const='.', # Default to current directory if flag is present but no value
        help="Search for the latest file (default .log, use --ext) in the specified directory and use it as input.\n" \
             "If flag is used without a directory, defaults to current directory ('.')."
    )
    input_group.add_argument(
        "--turn-range",
        metavar="START-END",
        help="Process 'TurnDebug_TurnX.csv' files for a given range (e.g., '5-10').\n" \
             "Requires --log-dir to specify the directory containing turn logs."
    )

    parser.add_argument(
        "--log-dir",
        default='.',
        help="Directory to search for logs when using --latest-from-dir or --turn-range.\n" \
             "Defaults to current directory ('.'). For UDebugObserverCSV, this might be 'Saved/TurnLogs'."
    )
    parser.add_argument(
        "--ext",
        default='log',
        help="File extension to search for when using --latest-from-dir (e.g., 'log', 'csv').\n" \
             "Defaults to 'log'."
    )

    parser.add_argument(
        "-k", "--keywords",
        nargs='+',
        help="Optional list of additional custom keywords to filter for.\n" \
             "These are typically used with '--depth medium'."
    )
    parser.add_argument(
        "-d", "--depth",
        choices=['critical', 'high', 'medium'],
        default='medium', # Default to medium, which includes custom keywords if provided
        help="Predefined filtering depth:\n" \
             "  critical: Only 'error:', 'fatal:', 'severe:'.\n" \
             "  high: Critical + 'warning:'.\n" \
             "  medium: High + custom keywords specified with -k (default)."
    )

    args = parser.parse_args()

    input_files_to_summarize = []
    
    # Handle mutually exclusive group and default behavior
    if args.input:
        input_files_to_summarize.append(args.input)
    elif args.turn_range:
        log_directory_for_turn_range = args.log_dir if args.log_dir != '.' else os.path.join(os.getcwd(), "Saved", "TurnLogs")
        print(f"Attempting to find turn logs in: {log_directory_for_turn_range}")

        try:
            start_turn, end_turn = map(int, args.turn_range.split('-'))
            for i in range(start_turn, end_turn + 1):
                turn_log_filename = os.path.join(log_directory_for_turn_range, f"TurnDebug_Turn{i}.csv")
                input_files_to_summarize.append(turn_log_filename)
        except ValueError:
            print(f"Error: Invalid --turn-range format. Expected 'START-END' (e.g., '5-10'). Got '{args.turn_range}'.")
            exit(1)
        
        if not input_files_to_summarize:
            print(f"Error: No turn log files found for range '{args.turn_range}' in '{log_directory_for_turn_range}'.")
            exit(1)
            
    else: # No specific input, no --turn-range. Default to latest log file search.
        search_dir = args.log_dir if args.log_dir else '.' # Use --log-dir if specified, otherwise current dir
        
        # Check if user explicitly provided --latest-from-dir without a value, in which case args.latest_from_dir is '.'
        # Or if no input method was chosen, args.latest_from-dir is None. In this case, default to '.'
        if args.latest_from_dir is None:
            print(f"No input file, --latest-from-dir, or --turn-range specified. Defaulting to searching for latest *.{args.ext} in current directory ('{search_dir}').")
            actual_input_file = find_latest_file(search_dir, args.ext)
            if not actual_input_file:
                # If default search fails, try the specific TurnLogs directory with .csv extension
                fallback_ext = 'csv'
                fallback_dir = os.path.join(os.getcwd(), '..', '..', '..', 'Saved', 'TurnLogs')
                print(f"No *.{args.ext} file found in '{search_dir}'. Falling back to UE default turn log directory: '{fallback_dir}' for *.{fallback_ext}")
                actual_input_file = find_latest_file(fallback_dir, fallback_ext)

        else:
            # If --latest-from-dir was explicitly used
            print(f"Searching for latest *.{args.ext} in specified directory ('{args.latest_from_dir}').")
            actual_input_file = find_latest_file(args.latest_from_dir, args.ext)

        if not actual_input_file:
            print("Exiting due to no input file found.")
            exit(1)
        input_files_to_summarize.append(actual_input_file)
        
    summarize_log(input_files_to_summarize, args.output, args.keywords, args.depth)
