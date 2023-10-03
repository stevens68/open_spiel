
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

#ifndef OPEN_SPIEL_GAMES_TWIXT_TWIXTBOARD_H_
#define OPEN_SPIEL_GAMES_TWIXT_TWIXTBOARD_H_

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <set>

#include "open_spiel/games/twixt/twixtcell.h"
#include "open_spiel/spiel.h"

namespace open_spiel {
namespace twixt {

const int kMinBoardSize = 5;
const int kMaxBoardSize = 24;
const int kDefaultBoardSize = 8;

const bool kDefaultAnsiColorOutput = true;

const double kMinDiscount = 0.0;
const double kMaxDiscount = 1.0;
const double kDefaultDiscount = kMaxDiscount;

// 8 link descriptors store the properties of a link direction
struct {
  Move offsets;  // offset of the target peg, e.g. (2, -1) for ENE
  std::vector<std::pair<Move, int>> blockingLinks;
} typedef LinkDescriptor;

// Tensor has 2 * 3 planes of size bordSize * (boardSize-2)
// see ObservationTensor
const int kNumPlanes = 6;

enum Result { kOpen, kRedWin, kBlueWin, kDraw };

enum Color { kRedColor, kBlueColor, kEmpty, kOffBoard };

// blockerMap stores set of blocking links for each link
static std::unordered_map<Link, std::set<Link>> blockerMap;

inline const std::set<Link>& GetBlockers(Link link) { return blockerMap[link]; }

inline void PushBlocker(Link link, Link blockedLink) {
  blockerMap[link].insert(blockedLink);
}

inline void DeleteBlocker(Link link, Link blockedLink) {
  blockerMap[link].erase(blockedLink);
}

inline void ClearBlocker() { blockerMap.clear(); }

class Board {
 private:
  int moveCounter_ = 0;
  bool swapped_ = false;
  Move moveOne_;
  int result_ = kOpen;
  std::vector<std::vector<Cell>> cell_;
  int size_;  // length of a side of the board
  bool ansiColorOutput_;
  std::vector<Action> legalActions_[kNumPlayers];

  void SetSize(int size) { size_ = size; }

  bool GetAnsiColorOutput() const { return ansiColorOutput_; }
  void SetAnsiColorOutput(bool ansiColorOutput) {
    ansiColorOutput_ = ansiColorOutput;
  }

  void SetResult(int result) { result_ = result; }

  bool GetSwapped() const { return swapped_; }
  void SetSwapped(bool swapped) { swapped_ = swapped; }

  Move GetMoveOne() const { return moveOne_; }
  void SetMoveOne(Move move) { moveOne_ = move; }

  void IncMoveCounter() { moveCounter_++; }

  bool HasLegalActions(Player player) const {
    return legalActions_[player].size() > 0;
  }

  void RemoveLegalAction(Player, Move);

  void UpdateResult(Player, Move);
  void UndoFirstMove();

  void InitializeCells(bool);
  void InitializeCandidates(Move, Cell&, bool);
  void InitializeBlockerMap(Move, int, LinkDescriptor&);

  void InitializeLegalActions();

  void SetPegAndLinks(Player, Move);
  void ExploreLocalGraph(Player, Cell&, enum Border);

  void AppendLinkChar(std::string&, Move, enum Compass, std::string) const;
  void AppendColorString(std::string&, std::string, std::string) const;
  void AppendPegChar(std::string&, Move) const;

  void AppendBeforeRow(std::string&, Move) const;
  void AppendPegRow(std::string&, Move) const;
  void AppendAfterRow(std::string&, Move) const;

  bool MoveIsOnBorder(Player, Move) const;
  bool MoveIsOffBoard(Move) const;

  Action StringToAction(std::string s) const;

 public:
  ~Board() {}
  Board() {}
  Board(int, bool);

  // std::string actionToString(Action) const;
  int GetSize() const { return size_; }
  std::string ToString() const;
  int GetResult() const { return result_; }
  int GetMoveCounter() const { return moveCounter_; }
  std::vector<Action> GetLegalActions(Player player) const {
    return legalActions_[player];
  }
  void ApplyAction(Player, Action);
  Cell& GetCell(Move move) { return cell_[move.first][move.second]; }
  const Cell& GetConstCell(Move move) const {
    return cell_[move.first][move.second];
  }
  Move ActionToMove(open_spiel::Player player, Action action) const;
  Action MoveToAction(Player player, Move move) const;
  Move GetTensorMove(Move move, int turn) const;
};

// twixt board:
// * the board has boardSize_ x boardSize_ cells
// * the x-axis (cols) points right,
// * the y axis (rows) points up
// * coords [col,row] start at the lower left corner [0,0]
// * coord labels c3, f4, d2, etc. start at the upper left corner (a1)
// * player 0 == 'x', red color, plays top/bottom
// * player 1 == 'o', blue color, plays left/right
// * move is labeled player + coord label, e.g. xd4
// * empty cell == 2
// * corner cell == 3
//
// example 8 x 8 board: red peg at [2,3] == xc5 == action=26
//                      red peg at [3,5] == xd3 == action=21
//                     blue peg at [5,3] == of5 == action=29
//
//     a   b   c   d   e   f   g   h
//    ------------------------------
// 1 | 3   2   2   2   2   2   2   3 |
//   |                               |
// 2 | 2   2   2   2   2   2   2   2 |
//   |                               |
// 3 | 2   2   2   0   2   2   2   2 |
//   |                               |
// 4 | 2   2   2   2   2   2   2   2 |
//   |                               |
// 5 | 2   2   0   2   2   1   2   2 |
//   |                               |
// 6 | 2   2   2   2   2   2   2   2 |
//   |                               |
// 7 | 2   2   2   2   2   2   2   2 |
//   |                               |
// 8 | 3   2   2   2   2   2   2   3 |
//     ------------------------------

// there's a red link from c5 to d3:
// cell[2][3].links = 00000001  (bit 1 set for NNE direction)
// cell[3][5].links = 00010000  (bit 5 set for SSW direction)

// Actions are indexed from 0 to boardSize * (boardSize-2) from the player's
// perspective:

// player 0 actions:
//     a   b   c   d   e   f   g   h
//    ------------------------------
// 1 |     7  15  23  31  39  47     |
//   |                               |
// 2 |     6  14  22  30  38  46     |
//   |                               |
// 3 |     5  13  21  29  37  45     |
//   |                               |
// 4 |     4  12  20  28  36  44     |
//   |                               |
// 5 |     3  11  19  27  35  43     |
//   |                               |
// 6 |     2  10  18  26  34  42     |
//   |                               |
// 7 |     1   9  17  25  33  41     |
//   |                               |
// 8 |     0   8  16  24  32  40     |
//     ------------------------------

// player 1 actions:
//     a   b   c   d   e   f   g   h
//    ------------------------------
// 1 |                               |
//   |                               |
// 2 | 0   1   2   3   4   5   6   7 |
//   |                               |
// 3 | 8   9  10  11  12  13  14  15 |
//   |                               |
// 4 |16  17  18  19  20  21  22  23 |
//   |                               |
// 5 |24  25  26  27  28  29  30  31 |
//   |                               |
// 6 |32  33  34  35  36  37  38  39 |
//   |                               |
// 7 |40  41  42  43  44  45  46  47 |
//   |                               |
// 8 |                               |
//     ------------------------------

//  mapping move to player 0 action:
//  [c,r] => (c-1) * size + r,
//  e.g.: xd6 == [3,2] => (3-1) * 8 + 2 == 18
//  xd6 == action 18 of player 0
//
//  mapping move to player 1 action:
//  [c,r] => (size-r-2) * size + c,
//  e.g.: od6 == [3,2] => (8-2-2) * 8 + 3 == 35
//  od6 == action 35 of player 1

}  // namespace twixt
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_TWIXT_TWIXTBOARD_H_
