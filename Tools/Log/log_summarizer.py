import argparse
import os
import glob
import re
import csv
import json
from datetime import datetime
from collections import deque

# CodeRevision: INC-2025-1121-COMPLETE - Added Roguelike specific debug presets (2025-11-21 06:00)

# --- プリセット定義 ---
PRESETS = {
    # ---------------------------------------------------------
    # 基本プリセット
    # ---------------------------------------------------------
    
    # 【デフォルト】 エラー調査用。
    'default': {
        'description': 'Errors, Warnings, and critical system events.',
        'whitelist': ['error', 'warning', 'fatal', 'exception', 'ensure', 'critical'],
        'blacklist': [
            'input window', 'latch reset', 'distancefield', 'dijkstra', 'compiling', 
            'rpc', 'gate opened', 'gate closed', 'client_tick', 'route check',
            'input_move_triggered', 'server_submitcommand', 'gridoccupancy'
        ]
    },
    
    # 【全表示】 フィルタなし
    'raw': {
        'description': 'No filtering. Shows everything.',
        'whitelist': [],
        'blacklist': []
    },

    # ---------------------------------------------------------
    # ゲームプレイ・AI 挙動解析
    # ---------------------------------------------------------

    # 【ゲームプレイ概要】 何が起きたか（行動、ダメージ、結果）
    'gameplay': {
        'description': 'Focus on game rules: Turns, Actions, Damage, Spawns.',
        'whitelist': [
            'turn', 'phase', 'damage', 'attack', 'move', 'dead', 'spawn', 
            'activate', 'commit', 'intent', 'health', 'ability', 'barrier',
            'gameplayevent', 'command received', 'level up', 'item'
        ],
        'blacklist': [
            'input window', 'latch reset', 'rpc', 'loading', 'init', 'compiling', 
            'garbage', 'texture', 'render', 'distancefield', 'dijkstra', 
            'find_latest_session', 'client_tick', 'input_move_triggered',
            'route check', 'gate opened', 'gate closed', 'sending gameplayevent',
            'pre-registering'
        ]
    },

    # 【AI思考分析】 なぜその行動を選んだか
    'ai_debug': {
        'description': 'Focus on AI decision making (Intents, Scoring).',
        'whitelist': [
            'intent', 'score', 'observation', 'behavior', 'think', 'enemy', 
            'computeintent', 'collectintents', 'hardblocked', 'pass1', 'pass2',
            'valid', 'invalid', 'fallback', 'logroguediagnostics', 'getnextstep'
        ],
        'blacklist': [
            'input', 'rendering', 'physics', 'texture', 'audio', 'animation',
            'input_move_triggered', 'movement component'
        ]
    },

    # ---------------------------------------------------------
    # システム・バグ調査（ローグライク特化）
    # ---------------------------------------------------------

    # 【移動ロジック・グリッド競合】 敵の重なり、壁抜け、予約エラー
    'movement_logic': {
        'description': 'Debug grid reservations, overlaps, and movement coordinates.',
        'whitelist': [
            'gridoccupancy', 'reserve', 'commit', 'reject', 'allow follow-up',
            'origin-hold', 'moveunit', 'movement component', 'dispatchresolvedmove',
            'updateactorcell', 'blocked', 'targetcell', 'cost', 'walkable', 'snap'
        ],
        'blacklist': [
            'input', 'rpc', 'render', 'audio', 'animation', 'gate', 'latch',
            'attack', 'damage', 'health', 'gameplayeffect'
        ]
    },

    # 【移動詳細デバッグ】 (NEW)
    # 経路探索のスコア、GetNextStepの詳細ログ
    'movement_detailed': {
        'description': 'Deep dive into pathfinding scores and neighbor selection.',
        'whitelist': [
            'getnextstep', 'logroguediagnostics', 'loggridpathfinding', 'logdistancefield',
            'neighbor', 'dist=', 'align=', 'diag=', 'accept', 'reject', 'candidate'
        ],
        'blacklist': [
            # 入出力や描画ノイズ
            'input', 'render', 'audio', 'animation',
            # RPC / ウィンドウID検証ノイズ（移動ロジック解析には不要）
            'submitcommand', 'windowid validated'
        ]
    },

    # 【入力フロー・スタッター】 操作のひっかかり、入力遅延
    'input_flow': {
        'description': 'Debug input latency, stuttering, and gate timing.',
        'whitelist': [
            'input_move_triggered', 'gate opened', 'gate closed', 'windowid',
            'waitingforplayerinput', 'blocked by gate', 'latch', 'ack', 'rpc',
            'submitcommand', 'command received', 'movecomplete', 'onallmovesfinished',
            'client_tick'
        ],
        'blacklist': [
            'distancefield', 'dijkstra', 'compiling', 'render', 'audio', 
            'animation', 'gridoccupancy', 'damage', 'health'
        ]
    },

    # 【ダンジョン生成・スポーン】 (NEW)
    # マップ生成失敗、敵が湧かない、開始地点がおかしい等の調査
    'dungeon_gen': {
        'description': 'Debug procedural generation, room placement, and initial spawns.',
        'whitelist': [
            'rogue', 'gen', 'seed', 'room', 'floor', 'spawn', 'buildunits',
            'streaming', 'level', 'load', 'navmesh', 'pathfinder', 'init',
            'playerstart', 'candidate', 'occupied'
        ],
        'blacklist': [
            'input', 'render', 'tick', 'camera', 'audio', 'animation', 'ui'
        ]
    },

    # 【GAS・ステータス・ダメージ計算】 (NEW)
    # ダメージ計算式、回復、バフ/デバフ、アビリティ発動失敗の調査
    'gas_stats': {
        'description': 'Debug Gameplay Ability System, Attributes, and Effects.',
        'whitelist': [
            'gameplayeffect', 'ge_', 'attribute', 'health', 'damage', 'heal',
            'activateability', 'endability', 'cost', 'cooldown', 'tag',
            'combatset', 'modifier', 'execute', 'grant'
        ],
        'blacklist': [
            'input', 'render', 'move', 'grid', 'transform', 'rotation'
        ]
    },

    # 【ターン進行停止・ハングアップ】 (NEW)
    # 「誰待ち？」の特定。バリアの残数、フェーズ遷移のスタックを調査
    'turn_hang': {
        'description': 'Debug turn flow, phase transitions, and barrier locks.',
        'whitelist': [
            'turn', 'phase', 'barrier', 'waiting', 'finish', 'complete', 
            'remaining', 'advance', 'notify', 'state', 'lock', 'registration'
        ],
        'blacklist': [
            'input', 'damage', 'render', 'audio', 'animation', 'ui', 'grid'
        ]
    }
}

def find_latest_session_log(log_directory):
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
    return latest_file or files[-1]

def summarize_log(lines, output_file, preset_name, custom_keywords=None, before=0, after=0, no_context=False):
    preset = PRESETS.get(preset_name, PRESETS['default'])
    
    # Filter setup
    critical_keywords = ['fatal', 'error', 'severe', 'exception', 'callstack', 'ensure']
    whitelist = [k.lower() for k in preset['whitelist']]
    blacklist = [k.lower() for k in preset['blacklist']]
    
    if custom_keywords:
        whitelist.extend([k.lower() for k in custom_keywords])
    
    print(f"--- Summarizing Mode: {preset_name} ---")
    print(f"Description: {preset['description']}")
    
    try:
        output_dir = os.path.dirname(output_file)
        if output_dir and not os.path.exists(output_dir):
            os.makedirs(output_dir, exist_ok=True)

        lines_written = 0
        with open(output_file, 'w', encoding='utf-8') as outfile:
            # コンテキスト行（前後）を付与するかどうか
            if no_context:
                max_before = 0
            else:
                max_before = before if (before > 0) else 2
            history = deque(maxlen=max_before)
            after_countdown = 0
            
            for line in lines:
                line_lower = line.lower()
                
                # 1. Check Critical (Always keep errors unless specifically raw mode)
                is_critical = any(c in line_lower for c in critical_keywords)
                
                # 2. Check Blacklist (Noise) - Skip unless critical
                if not is_critical and preset_name != 'raw':
                    if any(b in line_lower for b in blacklist):
                        continue
                
                # 3. Check Whitelist (Keep)
                matched = False
                if after_countdown > 0:
                    matched = True
                    after_countdown -= 1
                elif is_critical:
                    matched = True
                    # エラー時の自動コンテキストは no_context が False の場合のみ有効
                    if not no_context and before == 0 and after == 0:  # Auto context for errors
                        after_countdown = 4
                elif preset_name == 'raw':
                    matched = True
                else:
                    for w in whitelist:
                        if w in line_lower:
                            matched = True
                            after_countdown = after
                            break
                
                if matched:
                    while history:
                        outfile.write(history.popleft())
                        lines_written += 1
                    outfile.write(line)
                    lines_written += 1
                else:
                    if max_before > 0:
                        history.append(line)

        print(f"Done. Wrote {lines_written} lines to {output_file}")

    except Exception as e:
        print(f"Error: {e}")

def extract_player_actions(parsed_rows, row_turn_map, turns_to_include):
    actions = []
    for idx, row in enumerate(parsed_rows):
        if idx == 0 or len(row) < 5: continue
        turn_id = row_turn_map.get(idx)
        if turn_id is None: continue
        if turns_to_include and turn_id not in turns_to_include: continue

        msg = row[4]
        
        # AIモード: プレイヤーコマンドの確定のみを抽出
        if "Command.Player." not in msg: continue
        if "Command received and validated" not in msg and "Command validated" not in msg: continue

        tag_match = re.search(r"Tag=(Command\.Player\.[A-Za-z]+)", msg)
        tag = tag_match.group(1) if tag_match else "Unknown"
        
        target_cell = None
        target_match = re.search(r"TargetCell=\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", msg)
        if target_match:
            target_cell = {"x": int(target_match.group(1)), "y": int(target_match.group(2))}

        atype = "action"
        if ".Attack" in tag: atype = "attack"
        elif ".Move" in tag: atype = "move"
        elif ".Wait" in tag: atype = "wait"

        actions.append({
            "turn": turn_id,
            "action_type": atype,
            "tag": tag,
            "target_cell": target_cell,
            "timestamp": row[3],
            "raw": msg.strip()
        })
    return actions

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Smart Log Summarizer for UE5 Turn-Based Games")
    parser.add_argument("output", help="Output file path")
    parser.add_argument("input", nargs='?', help="Input log file (default: latest)")
    parser.add_argument("--log-dir", default=os.path.join(os.getcwd(), '..', '..', '..', 'Saved', 'TurnLogs'))
    
    # Filter Options
    parser.add_argument("--preset", choices=PRESETS.keys(), default='gameplay', help="Filter preset (default: gameplay)")
    parser.add_argument("--turn", type=str, help="Specific turn(s) (e.g., 5)")
    parser.add_argument("--turn-range", type=str, help="Turn range (e.g., 5-10)")
    parser.add_argument("-k", "--keywords", nargs='+', help="Extra keywords to include")
    parser.add_argument("--before", type=int, default=0)
    parser.add_argument("--after", type=int, default=0)
    parser.add_argument("--no-context", action="store_true",
                        help="Do not include extra context lines before/after matches or errors")
    
    # Mode Options
    parser.add_argument("--mode", choices=['summary', 'player_actions'], default='summary', help="Operation mode")
    parser.add_argument("--json", action="store_true", help="Output JSON (for player_actions)")

    args = parser.parse_args()

    # 1. File Setup
    input_file = args.input or find_latest_session_log(args.log_dir)
    if not input_file: exit(1)
    print(f"Reading: {input_file}")

    # Auto-redirect output to Tools/Log if no path provided
    output_path = args.output
    if not os.path.dirname(output_path):
        if os.path.exists(os.path.join('Tools', 'Log')):
            output_path = os.path.join('Tools', 'Log', output_path)
            print(f"Redirecting output to: {output_path}")
        args.output = output_path

    # 2. Encoding & Read
    try:
        with open(input_file, 'rb') as f: raw = f.read()
    except Exception as e: print(f"Read Error: {e}"); exit(1)
    
    text = None
    for enc in ('utf-8-sig', 'utf-16', 'utf-16-le'):
        try:
            cand = raw.decode(enc)
            if '\x00' not in cand: text = cand; break
        except: continue
    if not text: text = raw.decode('utf-8', 'ignore').replace('\x00', '')
    
    lines = text.splitlines(keepends=True)
    parsed_rows = list(csv.reader(lines))
    
    # 3. Turn Logic
    row_turn_map = {}
    max_turn = -1
    for i, row in enumerate(parsed_rows):
        if len(row) > 1 and row[1].strip().isdigit():
            t = int(row[1].strip())
            row_turn_map[i] = t
            if t > max_turn: max_turn = t
            
    turns = set()
    if not (args.turn or args.turn_range):
        if max_turn != -1: 
            s = max(0, max_turn - 2)
            turns.update(range(s, max_turn + 1))
            print(f"Defaulting to turns {s}-{max_turn}")
        else: turns = None
    else:
        if args.turn: turns.update(int(t) for t in args.turn.split(','))
        if args.turn_range:
            s, e = map(int, args.turn_range.split('-'))
            turns.update(range(s, e+1))

    # 4. Execution
    if args.mode == 'summary':
        filtered_lines = []
        if lines: filtered_lines.append(lines[0])
        for i in range(1, len(lines)):
            if turns is None or (i in row_turn_map and row_turn_map[i] in turns):
                filtered_lines.append(lines[i])
        
        if not filtered_lines: print("No lines found."); exit(0)
        summarize_log(
            filtered_lines,
            args.output,
            args.preset,
            args.keywords,
            before=args.before,
            after=args.after,
            no_context=args.no_context
        )

    elif args.mode == 'player_actions':
        actions = extract_player_actions(parsed_rows, row_turn_map, turns)
        if args.json:
            with open(args.output, 'w', encoding='utf-8') as f:
                json.dump(actions, f, indent=2, ensure_ascii=False)
        else:
            with open(args.output, 'w', encoding='utf-8') as f:
                for a in actions:
                    f.write(f"Turn {a['turn']}: {a['action_type'].upper()} {a['tag']} Target={a['target_cell']}\n")
        print(f"Extracted {len(actions)} player actions.")
