import argparse
import os
import glob
import re
import csv
import json
from datetime import datetime
from collections import deque

# CodeRevision: INC-2025-1200-R1 - Fix log_summarizer encoding handling and TurnID parsing for UTF-16 CSV logs (2025-11-21 02:00)
# CodeRevision: INC-2025-1201-R1 - Add AI-focused player_actions mode and JSON output for concise player turn summaries (2025-11-21 02:30)

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
            
            # lines is now a list of strings (raw lines) because we need to preserve formatting for output
            # But wait, if we use csv reader earlier, we might have lost the raw line.
            # Let's adjust the caller to pass raw lines or handle it here.
            # Actually, for summarization, we just need to search the text.
            # Let's assume 'lines' are the raw strings.
            
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


def extract_player_actions(parsed_rows, row_turn_map, turns_to_include):
    """
    Extract a concise, structured list of player commands (Command.Player.*)
    for the selected turns. Intended primarily for AI consumption.
    """
    actions = []

    for idx, row in enumerate(parsed_rows):
        # Skip header row and malformed rows
        if idx == 0 or len(row) < 5:
            continue

        turn_id = row_turn_map.get(idx)
        if turn_id is None:
            continue

        if turns_to_include and turn_id not in turns_to_include:
            continue

        msg = row[4]
        if "Command.Player." not in msg:
            continue

        # Focus on validated commands; this avoids transient or rejected inputs.
        if "Command received and validated" not in msg and "Command validated" not in msg:
            continue

        tag_match = re.search(r"Tag=(Command\.Player\.[A-Za-z]+)", msg)
        tag = tag_match.group(1) if tag_match else None

        target_cell = None
        target_match = re.search(r"TargetCell=\(([^)]*)\)", msg)
        if target_match:
            cell_str = target_match.group(1)
            try:
                parts = [c.strip() for c in cell_str.split(",")]
                if len(parts) == 2:
                    target_cell = {
                        "x": int(parts[0]),
                        "y": int(parts[1]),
                    }
            except ValueError:
                # If parsing fails, leave target_cell as None but keep the raw message.
                pass

        action_type = None
        if tag:
            if tag.endswith(".Attack"):
                action_type = "attack"
            elif tag.endswith(".Move"):
                action_type = "move"
            elif tag.endswith(".Wait"):
                action_type = "wait"

        actions.append(
            {
                "turn": turn_id,
                "tag": tag,
                "action_type": action_type,
                "timestamp": row[3] if len(row) > 3 else None,
                "category": row[2] if len(row) > 2 else None,
                "target_cell": target_cell,
                "raw_message": msg,
            }
        )

    return actions


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
    parser.add_argument("--mode", choices=['summary', 'player_actions'], default='summary', help="Output mode: 'summary' (keyword/context log summary) or 'player_actions' (concise player command list).")
    parser.add_argument("--json", action="store_true", help="When used with --mode player_actions, writes a JSON array of player actions instead of plain text.")

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
    
    # Read the file as bytes first so we can robustly detect UTF-8 vs UTF-16 encodings.
    try:
        with open(input_file, 'rb') as f:
            raw_bytes = f.read()
    except Exception as e:
        print(f"Error reading input file: {e}")
        exit(1)

    text = None
    used_encoding = None

    # Try a small set of likely encodings. We prefer decodes that do not contain NUL characters,
    # since seeing '\x00' between every character is a strong signal that a UTF-16 file was
    # mis-decoded as UTF-8.
    for enc in ('utf-8-sig', 'utf-16', 'utf-16-le'):
        try:
            candidate = raw_bytes.decode(enc)
        except UnicodeDecodeError:
            continue

        if '\x00' in candidate:
            # Treat embedded NULs as a decode failure for our purposes.
            continue

        text = candidate
        used_encoding = enc
        break

    if text is None:
        # Final fallback: decode as UTF-8 while ignoring errors, then strip any remaining NULs.
        text = raw_bytes.decode('utf-8', errors='ignore')
        used_encoding = 'utf-8 (errors=ignore)'
        if '\x00' in text:
            text = text.replace('\x00', '')

    all_lines = text.splitlines(keepends=True)
    print(f"Decoded log using encoding: {used_encoding}")

    # Parse CSV to determine turns
    # We use the csv module to parse the lines we just read
    parsed_rows = []
    try:
        # Using csv.reader on the list of lines
        reader = csv.reader(all_lines)
        parsed_rows = list(reader)
        
        # Aggressive debug output to understand how TurnID is being parsed.
        debug_sample_limit = 50
        with open('debug_log.txt', 'w', encoding='utf-8') as debug_file:
            debug_file.write(f"--- DEBUG: Encoding used: {used_encoding} ---\n")
            debug_file.write(f"--- DEBUG: First {min(debug_sample_limit, len(all_lines))} raw lines ---\n")

            for idx, line in enumerate(all_lines[:debug_sample_limit]):
                stripped = line.rstrip('\r\n')
                parts = stripped.split(',')
                debug_file.write(f"Line {idx} raw: {repr(stripped)}\n")
                debug_file.write(f"Line {idx} parts: {parts}\n")
                if len(parts) > 1:
                    field = parts[1]
                    printable = ''.join(ch for ch in field if ch.isprintable())
                    debug_file.write(f"Line {idx} parts[1]: {repr(field)}\n")
                    debug_file.write(f"Line {idx} printable parts[1]: {repr(printable)}\n")
                    debug_file.write(f"Line {idx} parts[1].strip().isdigit(): {field.strip().isdigit()}\n")
                debug_file.write("---\n")

            debug_file.write("\n--- DEBUG: First 5 parsed CSV rows ---\n")
            for i in range(min(5, len(parsed_rows))):
                debug_file.write(f"Row {i}: {parsed_rows[i]}\n")
            debug_file.write("-----------------------------------------\n")
        
    except Exception as e:
        print(f"Error parsing CSV content: {e}")
        exit(1)

    # --- Filter by Turn ---
    lines_to_process = []
    is_default_behavior = not (args.turn or args.turn_range)

    # Map row index to TurnID for filtering
    # We assume the structure: Type, TurnID, Category, Timestamp, Message
    # TurnID is at index 1
    row_turn_map = {}
    max_turn = -1

    for i, row in enumerate(parsed_rows):
        if len(row) > 1:
            turn_str = row[1].strip()
            if turn_str.isdigit():
                turn_id = int(turn_str)
                row_turn_map[i] = turn_id
                if turn_id > max_turn:
                    max_turn = turn_id

    turns_to_include = set()

    if is_default_behavior:
        print("No turn filter specified. Defaulting to last 3 turns.")
        
        if max_turn != -1:
            start_turn = max(0, max_turn - 2)
            args.turn_range = f"{start_turn}-{max_turn}"
            print(f"Found max turn {max_turn}. Applying default range: {args.turn_range}")
        else:
            print("Could not determine max turn. Processing all turns.")
            # Fallback: include every turn ID we observed.
            turns_to_include.update(row_turn_map.values())

    if args.turn or args.turn_range:
        turns_to_include.clear()
        
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

        for i in range(1, len(all_lines)):
            if i in row_turn_map and (not turns_to_include or row_turn_map[i] in turns_to_include):
                lines_to_process.append(all_lines[i])
    else:
        # Default behavior with a valid max_turn: populate turns_to_include and filtered lines.
        if max_turn != -1 and is_default_behavior:
            start_turn = max(0, max_turn - 2)
            turns_to_include.update(range(start_turn, max_turn + 1))

            if all_lines:
                lines_to_process.append(all_lines[0])  # Header
            for i in range(1, len(all_lines)):
                if i in row_turn_map and row_turn_map[i] in turns_to_include:
                    lines_to_process.append(all_lines[i])

    if args.mode == 'summary':
        if not lines_to_process:
            print("No lines matched the specified turn filters.")
            open(args.output, 'w').close()
            exit(0)

        summarize_log(lines_to_process, args.output, args.keywords, args.depth, args.before, args.after)
    elif args.mode == 'player_actions':
        # Prepare a concise, structured list of player actions for AI or tooling.
        if not turns_to_include:
            # If nothing was specified, fall back to all observed turn IDs.
            turns_to_include.update(row_turn_map.values())

        actions = extract_player_actions(parsed_rows, row_turn_map, turns_to_include)

        output_dir = os.path.dirname(args.output)
        if output_dir and not os.path.exists(output_dir):
            os.makedirs(output_dir, exist_ok=True)

        if args.json:
            with open(args.output, 'w', encoding='utf-8') as outfile:
                json.dump(actions, outfile, ensure_ascii=False, indent=2)
            print(f"Wrote {len(actions)} player actions to JSON file: {args.output}")
        else:
            # Text fallback: one action per line for quick inspection.
            with open(args.output, 'w', encoding='utf-8') as outfile:
                for action in actions:
                    tag = action.get("tag") or "Command.Player.Unknown"
                    action_type = action.get("action_type") or "unknown"
                    turn = action.get("turn")
                    target = action.get("target_cell")
                    target_str = f" target=({target['x']},{target['y']})" if target else ""
                    outfile.write(f"Turn {turn}: {action_type} [{tag}]{target_str} :: {action.get('raw_message')}\n")
            print(f"Wrote {len(actions)} player actions to text file: {args.output}")
