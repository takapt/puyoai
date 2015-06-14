#pragma once

#include <string>

class CoreField;
class RefPlan;
class RensaChainTrackResult;
struct PlayerState;

namespace peria {

struct Control;
struct PlayerHands;

// Evaluator evaluates something.
class Evaluator {
 public:
  Evaluator(const PlayerState&, const PlayerState&, const PlayerHands&, Control*);
  void EvalPlan(const RefPlan& plan);
  
 private:
  int EvalField(const CoreField& field, std::string* message);
  int EvalRensa(const RefPlan& plan, std::string* message);

  // Field related eval functions
  int PatternMatch(const CoreField& field, std::string* message);
  int Field(const CoreField& field);
  int Future(const CoreField& field);

  // Eval functions for Rensa.
  int EvalTsubushi(const RefPlan& plan);

  const PlayerState& me;
  const PlayerState& enemy;
  const PlayerHands& enemy_hands;
  Control* control;
};

}  // namespace peria
