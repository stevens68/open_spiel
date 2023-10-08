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

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "open_spiel/spiel_utils.h"
#include "open_spiel/games/twixt/twixt.h"
#include "open_spiel/games/twixt/twixtboard.h"
#include "open_spiel/utils/tensor_view.h"

namespace open_spiel {
namespace twixt {
namespace {

// Facts about the game.
const GameType kGameType{
    /*short_name=*/"twixt",
    /*long_name=*/"TwixT",
    GameType::Dynamics::kSequential,
    GameType::ChanceMode::kDeterministic,
    GameType::Information::kPerfectInformation,
    GameType::Utility::kZeroSum,
    GameType::RewardModel::kTerminal,
    /*max_num_players=*/2,
    /*min_num_players=*/2,
    /*provides_information_state_string=*/true,
    /*provides_information_state_tensor=*/false,
    /*provides_observation_string=*/true,
    /*provides_observation_tensor=*/true,
    /*parameter_specification=*/
    {{"board_size", GameParameter(kDefaultBoardSize)},
     {"ansi_color_output", GameParameter(kDefaultAnsiColorOutput)}},
};

std::unique_ptr<Game> Factory(const GameParameters &params) {
  return std::unique_ptr<Game>(new TwixTGame(params));
}

REGISTER_SPIEL_GAME(kGameType, Factory);

}  // namespace

TwixTState::TwixTState(std::shared_ptr<const Game> game) : State(game) {
  const TwixTGame &parent_game = static_cast<const TwixTGame &>(*game);
  board_ = Board(parent_game.GetBoardSize(), parent_game.GetAnsiColorOutput());
}

std::string TwixTState::ActionToString(open_spiel::Player player,
                                       Action action) const {
  Position position = board_.ActionToPosition(player, action);
  std::string s = (player == kRedPlayer) ? "x" : "o";
  s += static_cast<int>('a') + position.x;
  s.append(std::to_string(board_.size() - position.y));
  return s;
}

void TwixTState::SetPegAndLinksOnTensor(absl::Span<float> values,
                                        const Cell& cell, int offset, int turn,
                                        Position position) const {
  // we flip col/row here for better output in playthrough file
  TensorView<3> view(
      values, {kNumPlanes, board_.size(), board_.size() - 2}, false);
  Position tensorPosition = board_.GetTensorPosition(position, turn);

  if (!cell.HasLinks()) {
    // peg has no links -> use plane 0
    view[{0 + offset, tensorPosition.y, tensorPosition.x}] = 1.0;
  } else {
    // peg has links -> use plane 1
    view[{1 + offset, tensorPosition.y, tensorPosition.x}] = 1.0;
  }

  if (cell.HasBlockedNeighbors()) {
    // peg has blocked neighbors on plane 1 -> use also plane 2
    view[{2 + offset, tensorPosition.y, tensorPosition.x}] = 1.0;
  }
}

void TwixTState::ObservationTensor(open_spiel::Player player,
                                   absl::Span<float> values) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, kNumPlayers);

  const int kOpponentPlaneOffset = 3;
  const int kCurPlayerPlaneOffset = 0;
  int size = board_.size();

  // 6 planes of size boardSize x (boardSize-2):
  // each plane excludes the endlines of the opponent
  // planes 0 (3) are for the unlinked pegs of the current (opponent) player
  // planes 1 (4) are for the linked pegs of the current (opponent) player
  // planes 2 (5) are for the blocked pegs on plane 1 (4)

  // here we initialize Tensor with zeros for each state
  TensorView<3> view(
      values, {kNumPlanes, board_.size(), board_.size() - 2}, true);

  for (int c = 0; c < size; c++) {
    for (int r = 0; r < size; r++) {
      Position position = {c, r};
      const Cell& cell = board_.GetConstCell(position);
      int color = cell.color();
      if (player == kRedPlayer) {
        if (color == kRedColor) {
          // no turn
          SetPegAndLinksOnTensor(values, cell, kCurPlayerPlaneOffset,
            0, position);
        } else if (color == kBlueColor) {
          // 90 degr turn (blue player sits left side of red player)
          SetPegAndLinksOnTensor(values, cell, kOpponentPlaneOffset,
            90, position);
        }
      } else if (player == kBluePlayer) {
        if (color == kBlueColor) {
          // 90 degr turn
          SetPegAndLinksOnTensor(values, cell, kCurPlayerPlaneOffset,
            90, position);
        } else if (color == kRedColor) {
          // 90+90 degr turn (red player sits left of blue player)
          // setPegAndLinksOnTensor(values, cell, 5, size-c-2, size-r-1);
          SetPegAndLinksOnTensor(values, cell, kOpponentPlaneOffset,
            180, position);
        }
      }
    }
  }
}

TwixTGame::TwixTGame(const GameParameters &params)
    : Game(kGameType, params),
      ansi_color_output_(
          ParameterValue<bool>("ansi_color_output", kDefaultAnsiColorOutput)),
      board_size_(ParameterValue<int>("board_size", kDefaultBoardSize)) {
  if (board_size_ < kMinBoardSize || board_size_ > kMaxBoardSize) {
    SpielFatalError("board_size out of range [" +
                    std::to_string(kMinBoardSize) + ".." +
                    std::to_string(kMaxBoardSize) +
                    "]: " + std::to_string(board_size_));
  }
}

}  // namespace twixt
}  // namespace open_spiel
