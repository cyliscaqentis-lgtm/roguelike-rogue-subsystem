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


import argparse
import os
import glob
from datetime import datetime
from collections import deque

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


def summarize_log(input_files, output_file, custom_keywords=None, depth=None, before=0, after=0):
    """
    Reads log files, filters them for relevant lines based on keywords,
    and writes the result to an output file, including context lines.
    """
    # --- Keyword and Context Rule Setup ---
    user_specified_context = (before > 0 or after > 0)
    
    # Define keywords for different levels
    critical_keywords = ['fatal:', 'error:', 'severe:']
    high_keywords = critical_keywords + ['warning:']
    
    # Determine the final list of keywords to search for
    filter_keywords = []
    if depth == 'critical':
        filter_keywords.extend(critical_keywords)
    else: # high and medium
        filter_keywords.extend(high_keywords)
        if depth == 'medium' and custom_keywords:
            filter_keywords.extend([k.lower() for k in custom_keywords])
            
    filter_keywords = list(set([k.lower() for k in filter_keywords]))

    print(f"Starting log summarization...")
    print(f"Input files ({len(input_files)}): {input_files}")
    print(f"Output file: {output_file}")
    print(f"Filtering for keywords (depth={depth}): {filter_keywords}")
    if user_specified_context:
        print(f"Using user-specified context: Before={before}, After={after}")
    else:
        print("Using smart default context rules.")

    try:
        output_dir = os.path.dirname(output_file)
        if output_dir and not os.path.exists(output_dir):
            os.makedirs(output_dir, exist_ok=True)

        lines_written = 0
        with open(output_file, 'w', encoding='utf-8') as outfile:
            for input_file in input_files:
                print(f"Processing file: {input_file}")
                if not os.path.exists(input_file):
                    print(f"Warning: Input file not found, skipping: '{input_file}'")
                    continue
                
                with open(input_file, 'r', encoding='utf-8', errors='ignore') as infile:
                    # The maximum number of 'before' lines we need to store.
                    # If using smart defaults, this is the max of the default values.
                    max_before = before if user_specified_context else 5 # Max of (5, 1, 0)
                    history = deque(maxlen=max_before)
                    after_countdown = 0
                    
                    for line in infile:
                        line_lower = line.lower()
                        matched_keyword = None

                        # Write lines during an 'after' countdown
                        if after_countdown > 0:
                            outfile.write(line)
                            lines_written += 1
                            after_countdown -= 1
                            history.append(line)
                            continue

                        # Check for a keyword match
                        for keyword in filter_keywords:
                            if keyword in line_lower:
                                matched_keyword = keyword
                                break
                        
                        if matched_keyword:
                            # Determine context lines for this specific match
                            b, a = before, after
                            if not user_specified_context:
                                if any(k in matched_keyword for k in critical_keywords):
                                    b, a = 5, 2
                                elif 'warning:' in matched_keyword:
                                    b, a = 1, 1
                                else: # Custom keywords
                                    b, a = 0, 0

                            # Write 'before' context from history buffer
                            # Adjust history if b is smaller than max_before
                            context_to_write = list(history)[-b:]
                            for context_line in context_to_write:
                                outfile.write(context_line)
                            lines_written += len(context_to_write)
                            
                            history.clear() # Clear history to prevent duplicate context

                            # Write the matched line
                            outfile.write(line)
                            lines_written += 1
                            
                            # Start the 'after' countdown
                            after_countdown = a
                        else:
                            # If no match, just add to history
                            if max_before > 0:
                                history.append(line)

        print(f"Summarization complete. Wrote {lines_written} lines (including context).")

    except Exception as e:
        print(f"An unexpected error occurred: {e}")

def find_last_n_turn_logs(log_directory, n):
    """Finds the last N turn logs (e.g., TurnDebug_TurnX.csv) in a directory."""
    search_pattern = os.path.join(log_directory, 'TurnDebug_Turn*.csv')
    files = glob.glob(search_pattern)
    if not files:
        return []

    # Extract turn numbers from filenames
    turn_files = []
    for f in files:
        try:
            # Assumes filename is '...TurnDebug_Turn<NUMBER>.csv'
            turn_number_str = os.path.basename(f).split('Turn')[-1].split('.')[0]
            turn_number = int(turn_number_str)
            turn_files.append((turn_number, f))
        except (ValueError, IndexError):
            continue # Ignore files that don't match the pattern

    # Sort by turn number, descending
    turn_files.sort(key=lambda x: x[0], reverse=True)

    # Get the last N files
    return [f for _, f in turn_files[:n]]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Summarize Unreal Engine log files with context by filtering for specific keywords and predefined depths.",
        formatter_class=argparse.RawTextHelpFormatter
    )

    parser.add_argument("output", help="Path to the output summary file.")
    
    # --- Input Source Group ---
    input_group = parser.add_mutually_exclusive_group()
    input_group.add_argument("input", nargs='?', help="Path to a specific input log file.")
    input_group.add_argument("--latest-from-dir", nargs='?', const='.', help="Search for the latest file (default .log, use --ext) in the specified directory.")
    input_group.add_argument("--turn-range", metavar="START-END", help="Process 'TurnDebug_TurnX.csv' files for a given range (e.g., '5-10').")
    input_group.add_argument("--last-N-turns", type=int, metavar='N', help="Process the last N turn log files.")

    # --- General Options ---
    parser.add_argument("--log-dir", default=os.path.join(os.getcwd(), '..', '..', '..', 'Saved', 'TurnLogs'), help="Directory to search for logs. Defaults to the project's Saved/TurnLogs directory.")
    parser.add_argument("--ext", default='log', help="File extension to search for when using --latest-from-dir.")

    # --- Filtering and Context ---
    parser.add_argument("-k", "--keywords", nargs='+', help="Optional list of additional custom keywords.")
    parser.add_argument("-d", "--depth", choices=['critical', 'high', 'medium'], default='medium', help="Predefined filtering depth.")
    parser.add_argument("--before", type=int, default=0, help="Number of context lines to include before a match.")
    parser.add_argument("--after", type=int, default=0, help="Number of context lines to include after a match.")
    parser.add_argument("--context", type=int, help="Shorthand to set both --before and --after.")

    args = parser.parse_args()
    
    # --- Process Arguments ---
    if args.context is not None:
        args.before = args.context
        args.after = args.context

    input_files_to_summarize = []
    
    # --- Determine Input Files ---
    if args.input:
        input_files_to_summarize.append(args.input)
    elif args.latest_from_dir:
        input_files_to_summarize.append(find_latest_file(args.latest_from_dir, args.ext))
    elif args.turn_range:
        try:
            start_turn, end_turn = map(int, args.turn_range.split('-'))
            for i in range(start_turn, end_turn + 1):
                turn_log_filename = os.path.join(args.log_dir, f"TurnDebug_Turn{i}.csv")
                input_files_to_summarize.append(turn_log_filename)
        except ValueError:
            print(f"Error: Invalid --turn-range format. Expected 'START-END' (e.g., '5-10'). Got '{args.turn_range}'.")
            exit(1)
    elif args.last_N_turns:
        input_files_to_summarize = find_last_n_turn_logs(args.log_dir, args.last_N_turns)
    else:
        # NEW DEFAULT: Get last 3 turns
        print("No input specified. Defaulting to processing last 3 turn logs.")
        input_files_to_summarize = find_last_n_turn_logs(args.log_dir, 3)

    # --- Validate Input Files and Summarize ---
    # Filter out any None values from file searches that failed
    input_files_to_summarize = [f for f in input_files_to_summarize if f]
    
    if not input_files_to_summarize:
        print("Error: No input log files found after checking all options. Please specify a valid source.")
        exit(1)
        
    summarize_log(input_files_to_summarize, args.output, args.keywords, args.depth, args.before, args.after)
