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
#include "open_spiel/games/twixt/twixtcell.h"
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
  Position position = board_.ActionToPosition(action);
  std::string s = (player == kRedPlayer) ? "x" : "o";
  s += static_cast<int>('a') + position.x;
  s.append(std::to_string(board_.size() - position.y));
  return s;
}

void TwixTState::SetPegAndLinksOnTensor(absl::Span<float> values,
                                        const Cell& cell, int offset, bool turn,
                                        Position position) const {
  // we flip col/row here for better output in playthrough file
  TensorView<3> view(
      values, {kNumPlanes, board_.size(), board_.size() - 2}, false);
  Position tensorPosition = board_.GetTensorPosition(position, turn);

  if (!cell.HasLinks()) {
    // peg has no links -> plane 0 / 6
    view[{offset + 0, tensorPosition.y, tensorPosition.x}] = 1.0;
    return;
    
  } 

  for (int dir=0; dir<=4; dir++) {
    if (cell.HasLink(dir)) {
      // peg has link in direction dir -> plane 1..4 / 7..10
      view[{offset + 1 + dir, tensorPosition.y, tensorPosition.x}] = 1.0;
    }
  }

  if (cell.HasBlockedNeighbors()) {
    // peg has blocked links -> plane 5 / 11
    view[{offset + 5, tensorPosition.y, tensorPosition.x}] = 1.0;
  }
}

void TwixTState::ObservationTensor(open_spiel::Player player,
                                   absl::Span<float> values) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, kNumPlayers);

  const int kPlaneOffset[2] = {0, 6};
  int size = board_.size();

  // 2 x 6 planes of size boardSize x (boardSize-2):
  // each plane excludes the endlines of the opponent
  // plane 0/6 is for the unlinked pegs
  // plane 1..4 / 7..10 is for the links NNE, ENE, ESE, SSE, resp.
  // plane 5/11 is for blocked links 

  TensorView<3> view(
      values, {kNumPlanes, board_.size(), board_.size() - 2}, true);

  for (int c = 0; c < size; c++) {
    for (int r = 0; r < size; r++) {
      Position position = {c, r};
      const Cell& cell = board_.GetConstCell(position);
      int color = cell.color();
      if (color == kRedColor) {
        // no turn
        SetPegAndLinksOnTensor(values, cell, kPlaneOffset[0], false, position);
      } else if (color == kBlueColor) {
        // 90 degr turn 
        SetPegAndLinksOnTensor(values, cell, kPlaneOffset[1], true, position);
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
