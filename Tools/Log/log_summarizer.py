import argparse
import os
import glob
import re
from datetime import datetime
from collections import deque

def find_latest_session_log(log_directory):
    """Finds the session log with the most recent timestamp in its filename."""
    if not os.path.isdir(log_directory):
        print(f"Error: Log directory not found at '{log_directory}'")
        return None

    search_pattern = os.path.join(log_directory, 'Session_*.csv')
    files = glob.glob(search_pattern)
    if not files:
        print(f"Error: No Session_*.csv files found in '{log_directory}'")
        return None

    latest_file = None
    latest_timestamp = datetime.min

    for f in files:
        match = re.search(r'Session_(\d{8}-\d{6})\.csv', os.path.basename(f))
        if match:
            try:
                timestamp = datetime.strptime(match.group(1), '%Y%m%d-%H%M%S')
                if timestamp > latest_timestamp:
                    latest_timestamp = timestamp
                    latest_file = f
            except ValueError:
                continue

    if latest_file:
        print(f"Found latest session log: '{latest_file}'")
    else:
        print(f"No valid session logs found in '{log_directory}'")

    return latest_file

def summarize_log(lines, output_file, custom_keywords=None, depth=None, before=0, after=0):
    """
    Filters a list of log lines for relevant keywords and writes the
    result to an output file, including context lines.
    """
    user_specified_context = (before > 0 or after > 0)
    
    critical_keywords = ['fatal:', 'error:', 'severe:']
    high_keywords = critical_keywords + ['warning:']
    
    filter_keywords = []
    if depth == 'critical':
        filter_keywords.extend(critical_keywords)
    else:
        filter_keywords.extend(high_keywords)
        if depth == 'medium' and custom_keywords:
            filter_keywords.extend([k.lower() for k in custom_keywords])
            
    filter_keywords = list(set([k.lower() for k in filter_keywords]))

    print(f"Starting summarization...")
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
            max_before = before if user_specified_context else 5
            history = deque(maxlen=max_before)
            after_countdown = 0
            
            for line in lines:
                line_lower = line.lower()
                matched_keyword = None

                if after_countdown > 0:
                    outfile.write(line)
                    lines_written += 1
                    after_countdown -= 1
                    history.append(line)
                    continue

                for keyword in filter_keywords:
                    if keyword in line_lower:
                        matched_keyword = keyword
                        break
                
                if matched_keyword:
                    b, a = before, after
                    if not user_specified_context:
                        if any(k in matched_keyword for k in critical_keywords):
                            b, a = 5, 2
                        elif 'warning:' in matched_keyword:
                            b, a = 1, 1
                        else:
                            b, a = 0, 0

                    context_to_write = list(history)[-b:]
                    for context_line in context_to_write:
                        outfile.write(context_line)
                    lines_written += len(context_to_write)
                    
                    history.clear()
                    outfile.write(line)
                    lines_written += 1
                    after_countdown = a
                else:
                    if max_before > 0:
                        history.append(line)

        print(f"Summarization complete. Wrote {lines_written} lines (including context).")

    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Summarize Unreal Engine CSV log files with context. By default, processes the last 3 turns of the latest session log.",
        formatter_class=argparse.RawTextHelpFormatter
    )

    parser.add_argument("output", help="Path to the output summary file.")
    parser.add_argument("input", nargs='?', help="Path to a specific input session log file. If not specified, the latest session log will be used.")

    # --- Filtering and Context ---
    parser.add_argument("--log-dir", default=os.path.join(os.getcwd(), '..', '..', '..', 'Saved', 'TurnLogs'), help="Directory to search for session logs.")
    turn_filter_group = parser.add_mutually_exclusive_group()
    turn_filter_group.add_argument("--turn", type=str, help="Filter for a specific turn number, or a comma-separated list of turn numbers (e.g., '5' or '5,6,10').")
    turn_filter_group.add_argument("--turn-range", metavar="START-END", help="Filter for a range of turn numbers (e.g., '5-10').")
    
    parser.add_argument("-k", "--keywords", nargs='+', help="Optional list of additional custom keywords.")
    parser.add_argument("-d", "--depth", choices=['critical', 'high', 'medium'], default='medium', help="Predefined filtering depth.")
    parser.add_argument("--before", type=int, default=0, help="Number of context lines to include before a match.")
    parser.add_argument("--after", type=int, default=0, help="Number of context lines to include after a match.")
    parser.add_argument("--context", type=int, help="Shorthand to set both --before and --after.")

    args = parser.parse_args()
    
    if args.context is not None:
        args.before = args.context
        args.after = args.context

    # --- Determine and Read Input File ---
    input_file = args.input
    if not input_file:
        print("No input file specified, searching for the latest session log...")
        input_file = find_latest_session_log(args.log_dir)

    if not input_file or not os.path.exists(input_file):
        print(f"Error: Input log file not found at '{input_file}'.")
        exit(1)

    print(f"Reading input file: {input_file}")
    try:
        with open(input_file, 'r', encoding='utf-8-sig', errors='ignore') as f:
            all_lines = f.readlines()
    except Exception as e:
        print(f"Error reading input file: {e}")
        exit(1)


    # --- Filter by Turn ---
    lines_to_process = all_lines
    is_default_behavior = not (args.turn or args.turn_range)

    if is_default_behavior:
        print("No turn filter specified. Defaulting to last 3 turns.")
        max_turn = -1
        for line in reversed(all_lines):
            line = line.strip()
            if not line:
                continue
            try:
                parts = line.split(',')
                if len(parts) > 1 and parts[1].strip().isdigit():
                    turn_id = int(parts[1].strip())
                    if turn_id > max_turn:
                        max_turn = turn_id
            except (ValueError, IndexError):
                continue
        
        if max_turn != -1:
            start_turn = max(0, max_turn - 2)
            args.turn_range = f"{start_turn}-{max_turn}"
            print(f"Found max turn {max_turn}. Applying default range: {args.turn_range}")
        else:
            print("Could not determine max turn. Processing all turns.")

    if args.turn or args.turn_range:
        lines_to_process = []
        turns_to_include = set()
        
        if args.turn:
            try:
                turns_to_include.update(int(t.strip()) for t in args.turn.split(','))
            except ValueError:
                print(f"Error: Invalid --turn format. Got '{args.turn}'.")
                exit(1)
        if args.turn_range:
            try:
                start_turn, end_turn = map(int, args.turn_range.split('-'))
                turns_to_include.update(range(start_turn, end_turn + 1))
            except ValueError:
                print(f"Error: Invalid --turn-range format. Got '{args.turn_range}'.")
                exit(1)
        
        print(f"Filtering for Turn IDs: {sorted(list(turns_to_include))}")

        if all_lines:
            lines_to_process.append(all_lines[0]) # Always include header

        for line in all_lines[1:]:
            line = line.strip()
            if not line:
                continue
            try:
                parts = line.split(',')
                if len(parts) > 1 and parts[1].strip().isdigit():
                    turn_id = int(parts[1].strip())
                    if turn_id in turns_to_include:
                        lines_to_process.append(line + '\n') # Add newline back for processing
            except (ValueError, IndexError):
                continue

    if not lines_to_process:
        print("No lines matched the specified turn filters.")
        open(args.output, 'w').close()
        exit(0)

    summarize_log(lines_to_process, args.output, args.keywords, args.depth, args.before, args.after)
