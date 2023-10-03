
// Copyright 2019 DeepMind Technologies Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "open_spiel/games/twixt/twixtboard.h"
#include "open_spiel/games/twixt/twixtcell.h"

namespace open_spiel {
namespace twixt {

// ANSI colors
const char kAnsiRed[] = "\e[91m";
const char kAnsiBlue[] = "\e[94m";
const char kAnsiDefault[] = "\e[0m";

static std::pair<int, int> operator+(const std::pair<int, int> &l,
                                const std::pair<int, int> &r) {
  return {l.first + r.first, l.second + r.second};
}

// helper functions
inline int OppDir(int dir) { return (dir + kMaxCompass / 2) % kMaxCompass; }

inline int OppCand(int cand) { return cand < 16 ? cand <<= 4 : cand >>= 4; }

inline std::string MoveToString(Move move) {
  return "[" + std::to_string(move.first) + "," + std::to_string(move.second) +
         "]";
}

// table of 8 link descriptors
static std::vector<LinkDescriptor> kLinkDescriptorTable{
    // NNE
    {{1, 2},  // offset of target peg (2 up, 1 right)
     {        // blocking/blocked links
      {{0, 1}, kENE},
      {{-1, 0}, kENE},

      {{0, 2}, kESE},
      {{0, 1}, kESE},
      {{-1, 2}, kESE},
      {{-1, 1}, kESE},

      {{0, 1}, kSSE},
      {{0, 2}, kSSE},
      {{0, 3}, kSSE}}},
    // ENE
    {{2, 1},
     {{{0, -1}, kNNE},
      {{1, 0}, kNNE},

      {{-1, 1}, kESE},
      {{0, 1}, kESE},
      {{1, 1}, kESE},

      {{0, 1}, kSSE},
      {{0, 2}, kSSE},
      {{1, 1}, kSSE},
      {{1, 2}, kSSE}}},
    // ESE
    {{2, -1},
     {{{0, -1}, kNNE},
      {{1, -1}, kNNE},
      {{0, -2}, kNNE},
      {{1, -2}, kNNE},

      {{-1, -1}, kENE},
      {{0, -1}, kENE},
      {{1, -1}, kENE},

      {{0, 1}, kSSE},
      {{1, 0}, kSSE}}},
    // SSE
    {{1, -2},
     {{{0, -1}, kNNE},
      {{0, -2}, kNNE},
      {{0, -3}, kNNE},

      {{-1, -1}, kENE},
      {{0, -1}, kENE},
      {{-1, -2}, kENE},
      {{0, -2}, kENE},

      {{-1, 0}, kESE},
      {{0, -1}, kESE}}},
    // SSW
    {{-1, -2},
     {{{-1, -1}, kENE},
      {{-2, -2}, kENE},

      {{-2, 0}, kESE},
      {{-1, 0}, kESE},
      {{-2, -1}, kESE},
      {{-1, -1}, kESE},

      {{-1, 1}, kSSE},
      {{-1, 0}, kSSE},
      {{-1, -1}, kSSE}}},
    // WSW
    {{-2, -1},
     {{{-2, -2}, kNNE},
      {{-1, -1}, kNNE},

      {{-3, 0}, kESE},
      {{-2, 0}, kESE},
      {{-1, 0}, kESE},

      {{-2, 1}, kSSE},
      {{-1, 1}, kSSE},
      {{-2, 0}, kSSE},
      {{-1, 0}, kSSE}}},
    // WNW
    {{-2, 1},
     {{{-2, 0}, kNNE},
      {{-1, 0}, kNNE},
      {{-2, -1}, kNNE},
      {{-1, -1}, kNNE},

      {{-3, 0}, kENE},
      {{-2, 0}, kENE},
      {{-1, 0}, kENE},

      {{-2, 2}, kSSE},
      {{-1, 1}, kSSE}}},
    // NNW
    {{-1, 2},
     {{{-1, 1}, kNNE},
      {{-1, 0}, kNNE},
      {{-1, -1}, kNNE},

      {{-2, 1}, kENE},
      {{-1, 1}, kENE},
      {{-2, 0}, kENE},
      {{-1, 0}, kENE},

      {{-2, 2}, kESE},
      {{-1, 1}, kESE}}}
};

Board::Board(int size, bool ansiColorOutput) {
  SetSize(size);
  SetAnsiColorOutput(ansiColorOutput);

  InitializeCells(true);
  InitializeLegalActions();
}

void Board::InitializeBlockerMap(Move move, int dir, LinkDescriptor& ld) {
  Link link = {move, dir};
  for (auto &&entry : ld.blockingLinks) {
    Move fromMove = move + entry.first;
    if (!MoveIsOffBoard(fromMove)) {
      LinkDescriptor& oppLd = kLinkDescriptorTable[entry.second];
      Move toMove = move + entry.first + oppLd.offsets;
      if (!MoveIsOffBoard(toMove)) {
        PushBlocker(link, {fromMove, entry.second});
        PushBlocker(link, {toMove, OppDir(entry.second)});
      }
    }
  }
}

void Board::UpdateResult(Player player, Move move) {
  // check for WIN
  bool connectedToStart = GetCell(move).IsLinkedToBorder(player, kStart);
  bool connectedToEnd = GetCell(move).IsLinkedToBorder(player, kEnd);
  if (connectedToStart && connectedToEnd) {
    // peg is linked to both boarder lines
    SetResult(player == kRedPlayer ? kRedWin : kBlueWin);
    return;
  }

  // check if we are early in the game...
  if (GetMoveCounter() < GetSize() - 1) {
    // e.g. less than 5 moves played on a 6x6 board
    // => no win or draw possible, no need to update
    return;
  }

  // check if opponent (player to turn next) has any legal moves left
  if (!HasLegalActions(1 - player)) {
    SetResult(kDraw);
    return;
  }
}

void Board::InitializeCells(bool initBlockerMap) {
  cell_.resize(GetSize(), std::vector<Cell>(GetSize()));
  ClearBlocker();

  for (int x = 0; x < GetSize(); x++) {
    for (int y = 0; y < GetSize(); y++) {
      Move move = {x, y};
      Cell cell = GetCell(move);

      // set color to EMPTY or OFFBOARD
      if (MoveIsOffBoard(move)) {
        cell.SetColor(kOffBoard);
      } else {  // regular board
        cell.SetColor(kEmpty);
        if (x == 0) {
          cell.SetLinkedToBorder(kBluePlayer, kStart);
        } else if (x == GetSize() - 1) {
          cell.SetLinkedToBorder(kBluePlayer, kEnd);
        } else if (y == 0) {
          cell.SetLinkedToBorder(kRedPlayer, kStart);
        } else if (y == GetSize() - 1) {
          cell.SetLinkedToBorder(kRedPlayer, kEnd);
        }

        InitializeCandidates(move, cell, initBlockerMap);
      }
    }
  }
}

void Board::InitializeCandidates(Move move, Cell& cell, bool initBlockerMap) {
  for (int dir = 0; dir < kMaxCompass; dir++) {
    LinkDescriptor& ld = kLinkDescriptorTable[dir];
    Move targetMove = move + ld.offsets;
    if (!MoveIsOffBoard(targetMove)) {
      if (initBlockerMap) {
        InitializeBlockerMap(move, dir, ld);
      }
      cell.SetNeighbor(dir, targetMove);
      Cell targetCell = GetCell(targetMove);
      if (!(MoveIsOnBorder(kRedPlayer, move) &&
            MoveIsOnBorder(kBluePlayer, targetMove)) &&
          !(MoveIsOnBorder(kBluePlayer, move) &&
            MoveIsOnBorder(kRedPlayer, targetMove))) {
        cell.SetCandidate(kRedPlayer, dir);
        cell.SetCandidate(kBluePlayer, dir);
      }
    }
  }
}

void Board::InitializeLegalActions() {
  int numDistinctLegalActions = GetSize() * (GetSize() - 2);

  legalActions_[kRedPlayer].resize(numDistinctLegalActions);
  legalActions_[kBluePlayer].resize(numDistinctLegalActions);

  for (int player = kRedPlayer; player < kNumPlayers; player++) {
    std::vector<Action> *la = &legalActions_[player];
    la->clear();
    la->reserve(numDistinctLegalActions);

    for (Action a = 0; a < numDistinctLegalActions; a++) {
      la->push_back(a);
    }
  }
}

std::string Board::ToString() const {
  std::string s = "";

  // head line
  s.append("     ");
  for (int y = 0; y < GetSize(); y++) {
    std::string letter = "";
    letter += static_cast<int>('a') + y;
    letter += "  ";
    AppendColorString(s, kAnsiRed, letter);
  }
  s.append("\n");

  for (int y = GetSize() - 1; y >= 0; y--) {
    // print "before" row
    s.append("    ");
    for (int x = 0; x < GetSize(); x++) {
      AppendBeforeRow(s, {x, y});
    }
    s.append("\n");

    // print "peg" row
    GetSize() - y < 10 ? s.append("  ") : s.append(" ");
    AppendColorString(s, kAnsiBlue, std::to_string(GetSize() - y) + " ");
    for (int x = 0; x < GetSize(); x++) {
      AppendPegRow(s, {x, y});
    }
    s.append("\n");

    // print "after" row
    s.append("    ");
    for (int x = 0; x < GetSize(); x++) {
      AppendAfterRow(s, {x, y});
    }
    s.append("\n");
  }
  s.append("\n");

  if (swapped_)
    s.append("[swapped]");

  switch (result_) {
  case kOpen:
    break;
  case kRedWin:
    s.append("[x has won]");
    break;
  case kBlueWin:
    s.append("[o has won]");
    break;
  case kDraw:
    s.append("[draw]");
  default:
    break;
  }

  return s;
}

void Board::AppendLinkChar(std::string& s, Move move, enum Compass dir,
                           std::string linkChar) const {
  if (!MoveIsOffBoard(move) && GetConstCell(move).HasLink(dir)) {
    if (GetConstCell(move).GetColor() == kRedColor) {
      AppendColorString(s, kAnsiRed, linkChar);
    } else if (GetConstCell(move).GetColor() == kBlueColor) {
      AppendColorString(s, kAnsiBlue, linkChar);
    } else {
      s.append(linkChar);
    }
  }
}

void Board::AppendColorString(std::string& s, std::string colorString,
                              std::string appString) const {
  s.append(GetAnsiColorOutput() ? colorString : "");  // make it colored
  s.append(appString);
  s.append(GetAnsiColorOutput() ? kAnsiDefault : "");  // make it default
}

void Board::AppendPegChar(std::string& s, Move move) const {
  if (GetConstCell(move).GetColor() == kRedColor) {
    // x
    AppendColorString(s, kAnsiRed, "x");
  } else if (GetConstCell(move).GetColor() == kBlueColor) {
    // o
    AppendColorString(s, kAnsiBlue, "o");
  } else if (MoveIsOffBoard(move)) {
    // corner
    s.append(" ");
  } else if (move.first == 0 || move.first == GetSize() - 1) {
    // empty . (blue border line)
    AppendColorString(s, kAnsiBlue, ".");
  } else if (move.second == 0 || move.second == GetSize() - 1) {
    // empty . (red border line)
    AppendColorString(s, kAnsiRed, ".");
  } else {
    // empty (non border line)
    s.append(".");
  }
}

void Board::AppendBeforeRow(std::string& s, Move move) const {
  // -1, +1
  int len = s.length();
  AppendLinkChar(s, move + (Move){-1, 0}, kENE, "/");
  AppendLinkChar(s, move + (Move){-1, -1}, kNNE, "/");
  AppendLinkChar(s, move + (Move){0, 0}, kWNW, "_");
  if (len == s.length())
    s.append(" ");

  //  0, +1
  len = s.length();
  AppendLinkChar(s, move, kNNE, "|");
  if (len == s.length())
    AppendLinkChar(s, move, kNNW, "|");
  if (len == s.length())
    s.append(" ");

  // +1, +1
  len = s.length();
  AppendLinkChar(s, move + (Move){+1, 0}, kWNW, "\\");
  AppendLinkChar(s, move + (Move){+1, -1}, kNNW, "\\");
  AppendLinkChar(s, move + (Move){0, 0}, kENE, "_");
  if (len == s.length())
    s.append(" ");
}

void Board::AppendPegRow(std::string& s, Move move) const {
  // -1, 0
  int len = s.length();
  AppendLinkChar(s, move + (Move){-1, -1}, kNNE, "|");
  AppendLinkChar(s, move + (Move){0, 0}, kWSW, "_");
  if (len == s.length())
    s.append(" ");

  //  0,  0
  AppendPegChar(s, move);

  // +1, 0
  len = s.length();
  AppendLinkChar(s, move + (Move){+1, -1}, kNNW, "|");
  AppendLinkChar(s, move + (Move){0, 0}, kESE, "_");
  if (len == s.length())
    s.append(" ");
}

void Board::AppendAfterRow(std::string& s, Move move) const {
  // -1, -1
  int len = s.length();
  AppendLinkChar(s, move + (Move){+1, -1}, kWNW, "\\");
  AppendLinkChar(s, move + (Move){0, -1}, kNNW, "\\");
  if (len == s.length())
    s.append(" ");

  //  0, -1
  len = s.length();
  AppendLinkChar(s, move + (Move){-1, -1}, kENE, "_");
  AppendLinkChar(s, move + (Move){+1, -1}, kWNW, "_");
  AppendLinkChar(s, move, kSSW, "|");
  if (len == s.length())
    AppendLinkChar(s, move, kSSE, "|");
  if (len == s.length())
    s.append(" ");

  // -1, -1
  len = s.length();
  AppendLinkChar(s, move + (Move){-1, -1}, kENE, "/");
  AppendLinkChar(s, move + (Move){0, -1}, kNNE, "/");
  if (len == s.length())
    s.append(" ");
}

void Board::UndoFirstMove() {
  Cell& cell = GetCell(GetMoveOne());
  cell.SetColor(kEmpty);
  // initialize Candidates but not static blockerMap
  InitializeCandidates(GetMoveOne(), cell, false);
  InitializeLegalActions();
}

void Board::ApplyAction(Player player, Action action) {
  Move move = ActionToMove(player, action);

  if (GetMoveCounter() == 1) {
    // it's the second move
    if (move == GetMoveOne()) {
      // blue player swapped
      SetSwapped(true);

      // undo the first move (peg and legal actions)
      UndoFirstMove();

      // turn move 90° clockwise: [3,2] -> [5,3]
      int col = GetSize() - move.second - 1;
      int row = move.first;
      move = {col, row};

    } else {
      // blue player hasn't swapped => regular move
      // remove move one from legal moves
      RemoveLegalAction(kRedPlayer, GetMoveOne());
      RemoveLegalAction(kBluePlayer, GetMoveOne());
    }
  }

  SetPegAndLinks(player, move);

  if (GetMoveCounter() == 0) {
    // do not remove the move from legal actions but store it
    // because second player might want to swap, by choosing the same move
    SetMoveOne(move);
  } else {
    // otherwise remove move from legal actions
    RemoveLegalAction(kRedPlayer, move);
    RemoveLegalAction(kBluePlayer, move);
  }

  IncMoveCounter();

  // Update the predicted result and update mCurrentPlayer...
  UpdateResult(player, move);
}

void Board::SetPegAndLinks(Player player, Move move) {
  bool linkedToNeutral = false;
  bool linkedToStart = false;
  bool linkedToEnd = false;

  // set peg
  Cell& cell = GetCell(move);
  cell.SetColor(player);

  int dir = 0;
  bool newLinks = false;
  // check all candidates (neigbors that are empty or have same color)
  for (int cand = 1, dir = 0; cand <= cell.GetCandidates(player);
       cand <<= 1, dir++) {
    if (cell.IsCandidate(player, cand)) {
      Move n = cell.GetNeighbor(dir);

      Cell& targetCell = GetCell(cell.GetNeighbor(dir));
      if (targetCell.GetColor() == kEmpty) {
        // pCell is not a candidate for pTarGetCell anymore
        // (from opponent's perspective)
        targetCell.DeleteCandidate(1 - player, OppCand(cand));
      } else {
        // check if there are blocking links before setting link
        const std::set<Link>& blockers = GetBlockers((Link){move, dir});
        bool blocked = false;
        for (auto &bl : blockers) {
          if (GetCell(bl.first).HasLink(bl.second)) {
            blocked = true;
            break;
          }
        }

        if (!blocked) {
          // we set the link, and set the flag that there is at least one new
          // link
          cell.SetLink(dir);
          targetCell.SetLink(OppDir(dir));

          newLinks = true;

          // check if cell we link to is linked to START border / END border
          if (targetCell.IsLinkedToBorder(player, kStart)) {
            cell.SetLinkedToBorder(player, kStart);
            linkedToStart = true;
          } else if (targetCell.IsLinkedToBorder(player, kEnd)) {
            cell.SetLinkedToBorder(player, kEnd);
            linkedToEnd = true;
          } else {
            linkedToNeutral = true;
          }
        } else {
          // we store the fact that these two pegs of the same color cannot be
          // linked this info is used for the ObservationTensor
          cell.SetBlockedNeighbor(cand);
          targetCell.SetBlockedNeighbor(OppCand(cand));
        }
      }  // is not empty
    }  // is candidate
  }  // candidate range

  // check if we need to explore further
  if (newLinks) {
    if (cell.IsLinkedToBorder(player, kStart) && linkedToNeutral) {
      // case: new cell is linked to START and linked to neutral cells
      // => explore neutral graph and add all its cells to START
      ExploreLocalGraph(player, cell, kStart);
    }
    if (cell.IsLinkedToBorder(player, kEnd) && linkedToNeutral) {
      // case: new cell is linked to END and linked to neutral cells
      // => explore neutral graph and add all its cells to END
      ExploreLocalGraph(player, cell, kEnd);
    }
  }
}

void Board::ExploreLocalGraph(Player player, Cell& cell, enum Border border) {
  int dir = 0;
  for (int link = 1, dir = 0; link <= cell.GetLinks(); link <<= 1, dir++) {
    if (cell.IsLinked(link)) {
      Cell& targetCell = GetCell(cell.GetNeighbor(dir));
      if (!targetCell.IsLinkedToBorder(player, border)) {
        // linked neighbor is NOT yet member of PegSet
        // => add it and explore
        targetCell.SetLinkedToBorder(player, border);
        ExploreLocalGraph(player, targetCell, border);
      }
    }
  }
}

Move Board::GetTensorMove(Move move, int turn) const {
  switch (turn) {
  case 0:
    return {move.first - 1, move.second};
    break;
  case 90:
    return {GetSize() - move.second - 2, move.first};
    break;
  case 180:
    return {GetSize() - move.first - 2, GetSize() - move.second - 1};
    break;
  default:
    SpielFatalError("invalid turn: " + std::to_string(turn) +
                    "; should be 0, 90, 180");
  }
}

Move Board::ActionToMove(open_spiel::Player player, Action action) const {
  Move move;
  if (player == kRedPlayer) {
    move.first = action / size_ + 1;  // col
    move.second = action % size_;     // row
  } else {
    move.first = action % size_;                 // col
    move.second = size_ - (action / size_) - 2;  // row
  }
  return move;
}

Action Board::MoveToAction(Player player, Move move) const {
  Action action;
  if (player == kRedPlayer) {
    action = (move.first - 1) * size_ + move.second;
  } else {
    action = (size_ - move.second - 2) * size_ + move.first;
  }
  return action;
}

Action Board::StringToAction(std::string s) const {
  Player player = (s.at(0) == 'x') ? kRedPlayer : kBluePlayer;
  Move move;
  move.first = static_cast<int>(s.at(1)) - static_cast<int>('a');
  move.second = GetSize() - (static_cast<int>(s.at(2)) - static_cast<int>('0'));
  return MoveToAction(player, move);
}

bool Board::MoveIsOnBorder(Player player, Move move) const {
  if (player == kRedPlayer) {
    return ((move.second == 0 || move.second == GetSize() - 1) &&
            (move.first > 0 && move.first < GetSize() - 1));
  } else {
    return ((move.first == 0 || move.first == GetSize() - 1) &&
            (move.second > 0 && move.second < GetSize() - 1));
  }
}

bool Board::MoveIsOffBoard(Move move) const {
  return (move.second < 0 || move.second > GetSize() - 1 || move.first < 0 ||
          move.first > GetSize() - 1 ||
          // corner case
          ((move.first == 0 || move.first == GetSize() - 1) &&
           (move.second == 0 || move.second == GetSize() - 1)));
}

void Board::RemoveLegalAction(Player player, Move move) {
  Action action = MoveToAction(player, move);
  std::vector<Action> *la = &legalActions_[player];
  std::vector<Action>::iterator it;
  it = find(la->begin(), la->end(), action);
  if (it != la->end())
    la->erase(it);
}

}  // namespace twixt
}  // namespace open_spiel
