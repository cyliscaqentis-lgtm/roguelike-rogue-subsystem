import argparse
import os

# This script is designed to parse and summarize Unreal Engine log files.
# It filters for specific keywords like 'Error:' and 'Warning:' to reduce
# the log size, making it more manageable for AI context windows while
# retaining the most critical information.

def summarize_log(input_file, output_file, custom_keywords=None):
    """
    Reads a log file, filters it for relevant lines based on keywords,
    and writes the result to an output file.

    Args:
        input_file (str): Path to the input log file.
        output_file (str): Path to the output summary file.
        custom_keywords (list, optional): A list of additional keywords to filter for.
                                           Defaults to None.
    """
    # Standard keywords to filter for in UE logs.
    # Using lowercase for case-insensitive matching.
    filter_keywords = ['error:', 'warning:', 'severe:', 'fatal:']
    if custom_keywords:
        filter_keywords.extend([k.lower() for k in custom_keywords])

    print(f"Starting log summarization...")
    print(f"Input file: {input_file}")
    print(f"Output file: {output_file}")
    print(f"Filtering for keywords: {filter_keywords}")

    try:
        # Ensure output directory exists
        output_dir = os.path.dirname(output_file)
        if output_dir:
            os.makedirs(output_dir, exist_ok=True)

        lines_written = 0
        # Use UTF-8 encoding, as it's standard for UE logs and per project guidelines.
        with open(input_file, 'r', encoding='utf-8', errors='ignore') as infile, \
             open(output_file, 'w', encoding='utf-8') as outfile:

            for line in infile:
                # Perform a case-insensitive check for any of the keywords
                if any(keyword in line.lower() for keyword in filter_keywords):
                    outfile.write(line)
                    lines_written += 1

        print(f"Summarization complete. Found and wrote {lines_written} relevant lines.")

    except FileNotFoundError:
        print(f"Error: Input file not found at '{input_file}'")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Summarize Unreal Engine log files by filtering for specific keywords.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "input",
        help="Path to the input log file."
    )
    parser.add_argument(
        "output",
        help="Path to the output summary file."
    )
    parser.add_argument(
        "-k", "--keywords",
        nargs='+',
        help="Optional list of additional custom keywords to filter for.\n" \
             "Example: -k LogTurnManager LogAbilities"
    )

    args = parser.parse_args()

    summarize_log(args.input, args.output, args.keywords)
