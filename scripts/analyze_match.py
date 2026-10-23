#!/usr/bin/env python3
"""
analyze_match.py — Parse match PGN to extract:
  - Total games, W/D/L, score, Elo with 95% error bars
  - Score split by color (new engine as White vs as Black)
  - Crashes, illegal moves, and time losses per engine
"""

import sys
import os
import re
import math
from pathlib import Path

def parse_pgn(pgn_path: Path, engine1_name: str, engine2_name: str):
    if not pgn_path.is_file():
        print(f"Error: PGN file not found: {pgn_path}", file=sys.stderr)
        sys.exit(1)

    games = []
    current_game = {}
    
    with open(pgn_path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    # Split games by [Event
    raw_games = content.split("[Event ")
    for rg in raw_games:
        if not rg.strip():
            continue
        full_game = "[Event " + rg

        # Extract headers
        white_match = re.search(r'\[White "([^"]+)"\]', full_game)
        black_match = re.search(r'\[Black "([^"]+)"\]', full_game)
        result_match = re.search(r'\[Result "([^"]+)"\]', full_game)
        term_match = re.search(r'\[Termination "([^"]+)"\]', full_game)

        if not (white_match and black_match and result_match):
            continue

        white = white_match.group(1).strip()
        black = black_match.group(1).strip()
        result = result_match.group(1).strip()
        termination = term_match.group(1).strip() if term_match else "unknown"

        # Look for comment inside braces at end of game
        comments = re.findall(r'\{([^}]+)\}', full_game)
        final_comment = comments[-1] if comments else ""

        games.append({
            "white": white,
            "black": black,
            "result": result,
            "termination": termination,
            "comment": final_comment
        })

    return games

def calculate_elo(wins, draws, losses):
    total = wins + draws + losses
    if total == 0:
        return 0.0, 0.0, 0.0

    score = (wins + 0.5 * draws) / total
    if score <= 0.0:
        return score, -999.0, 0.0
    if score >= 1.0:
        return score, 999.0, 0.0

    elo = -400.0 * math.log10((1.0 - score) / score)

    # Variance and standard error
    dev_w = (1.0 - score) ** 2
    dev_d = (0.5 - score) ** 2
    dev_l = (0.0 - score) ** 2
    variance = (wins * dev_w + draws * dev_d + losses * dev_l) / total
    se_score = math.sqrt(variance / total) if total > 1 else 0.0

    # Elo error margin (95% CI -> 1.96 * SE)
    d_elo_d_score = 400.0 / (math.log(10) * score * (1.0 - score))
    elo_margin = 1.96 * se_score * d_elo_d_score

    return score, elo, elo_margin

def analyze(pgn_path: Path, engine1_name: str, engine2_name: str):
    games = parse_pgn(pgn_path, engine1_name, engine2_name)
    total_games = len(games)

    if total_games == 0:
        print("No completed games found in PGN.", file=sys.stderr)
        sys.exit(1)

    # Stats from perspective of engine2 ("new engine")
    # Overall
    e2_wins = 0
    e2_draws = 0
    e2_losses = 0

    # Color split
    e2_as_white_w = 0
    e2_as_white_d = 0
    e2_as_white_l = 0

    e2_as_black_w = 0
    e2_as_black_d = 0
    e2_as_black_l = 0

    # Anomalies
    crashes = {engine1_name: 0, engine2_name: 0}
    illegal_moves = {engine1_name: 0, engine2_name: 0}
    time_losses = {engine1_name: 0, engine2_name: 0}

    for g in games:
        w = g["white"]
        b = g["black"]
        res = g["result"]
        comment = g["comment"].lower()
        term = g["termination"].lower()

        # Check anomalies
        if "crashed" in comment or "disconnected" in comment or "stall" in comment:
            if "white" in comment:
                crashes[w] = crashes.get(w, 0) + 1
            elif "black" in comment:
                crashes[b] = crashes.get(b, 0) + 1
        elif "illegal move" in comment:
            if "white" in comment:
                illegal_moves[w] = illegal_moves.get(w, 0) + 1
            elif "black" in comment:
                illegal_moves[b] = illegal_moves.get(b, 0) + 1
        elif "time" in comment or "time forfeit" in term:
            if "white wins on time" in comment:
                time_losses[b] = time_losses.get(b, 0) + 1
            elif "black wins on time" in comment:
                time_losses[w] = time_losses.get(w, 0) + 1
            elif "white loses on time" in comment:
                time_losses[w] = time_losses.get(w, 0) + 1
            elif "black loses on time" in comment:
                time_losses[b] = time_losses.get(b, 0) + 1

        # Score attribution
        if w == engine2_name:
            if res == "1-0":
                e2_wins += 1
                e2_as_white_w += 1
            elif res == "0-1":
                e2_losses += 1
                e2_as_white_l += 1
            elif res == "1/2-1/2":
                e2_draws += 1
                e2_as_white_d += 1
        elif b == engine2_name:
            if res == "0-1":
                e2_wins += 1
                e2_as_black_w += 1
            elif res == "1-0":
                e2_losses += 1
                e2_as_black_l += 1
            elif res == "1/2-1/2":
                e2_draws += 1
                e2_as_black_d += 1

    score, elo, elo_margin = calculate_elo(e2_wins, e2_draws, e2_losses)
    score_w, _, _ = calculate_elo(e2_as_white_w, e2_as_white_d, e2_as_white_l)
    score_b, _, _ = calculate_elo(e2_as_black_w, e2_as_black_d, e2_as_black_l)

    tot_white_games = e2_as_white_w + e2_as_white_d + e2_as_white_l
    tot_black_games = e2_as_black_w + e2_as_black_d + e2_as_black_l

    print("\n" + "=" * 65)
    print(f"MATCH REPORT: {engine2_name} vs {engine1_name}")
    print("=" * 65)
    print(f"Total Games: {total_games}")
    print(f"Overall Result (from {engine2_name} perspective):")
    print(f"  Wins: {e2_wins} | Draws: {e2_draws} | Losses: {e2_losses}")
    print(f"  Score: {(e2_wins + 0.5 * e2_draws):.1f} / {total_games} ({score * 100:.2f}%)")
    print(f"  Elo: {elo:+.2f} +/- {elo_margin:.2f}")

    print("\nScore Split by Color:")
    if tot_white_games > 0:
        pts_w = e2_as_white_w + 0.5 * e2_as_white_d
        print(f"  {engine2_name} as White: {e2_as_white_w}W - {e2_as_white_d}D - {e2_as_white_l}L | {pts_w:.1f}/{tot_white_games} ({score_w * 100:.2f}%)")
    if tot_black_games > 0:
        pts_b = e2_as_black_w + 0.5 * e2_as_black_d
        print(f"  {engine2_name} as Black: {e2_as_black_w}W - {e2_as_black_d}D - {e2_as_black_l}L | {pts_b:.1f}/{tot_black_games} ({score_b * 100:.2f}%)")

    print("\nGame Terminations / Anomalies:")
    print(f"  Crashes:        {engine1_name}={crashes.get(engine1_name, 0)}, {engine2_name}={crashes.get(engine2_name, 0)}")
    print(f"  Illegal moves:  {engine1_name}={illegal_moves.get(engine1_name, 0)}, {engine2_name}={illegal_moves.get(engine2_name, 0)}")
    print(f"  Time losses:    {engine1_name}={time_losses.get(engine1_name, 0)}, {engine2_name}={time_losses.get(engine2_name, 0)}")
    print("=" * 65)

def main():
    if len(sys.argv) < 4:
        print("Usage: analyze_match.py <pgn_path> <engine1_name> <engine2_name>", file=sys.stderr)
        sys.exit(1)
    
    pgn_path = Path(sys.argv[1])
    engine1_name = sys.argv[2]
    engine2_name = sys.argv[3]
    analyze(pgn_path, engine1_name, engine2_name)

if __name__ == "__main__":
    main()

